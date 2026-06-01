# Sherpa `missing_final_transcript` Root-Cause Fix Plan

## Problem Summary
When clicking **Transcribe** and speaking `请讲一个笑话`, the UI ends with:

- `Recognition failed`
- `speech transcribe missing_final_transcript: speech final response did not contain transcript text`

## Root-Cause Analysis (Code-Based)

### 1) Preview requests are not flagged as preview in outbound `speech.transcribe` payload
- File: `blazeclaw/BlazeClawMfc/web/chat/chat-controller.js`
- Function: `transcribeSpeech`
- The local variable `livePreviewOnly` is computed, but the request object sent to `speech.transcribe` does **not** include `livePreviewOnly`.
- Native side reads this flag from request params (`GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`) and defaults to `false` when missing.

Impact:
- Native builds non-preview streaming contract for preview ticks.
- Preview path loses preview-specific chunk/lookback behavior and can underperform token emission before stop.

### 2) Native Sherpa streaming returns `ok=true` + `stage=completed` even when no final transcript exists
- File: `blazeclaw/BlazeClawMfc/src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- Function: `TranscribeStreaming`
- If no tokens/text are produced (`no_speech_detected` / `no_tokens_emitted`), code still sets:
  - `result.ok = true`
  - `result.sessionState.stage = SpeechSessionStage::Completed`
  - empty transcript text

Impact:
- Frontend receives a successful-but-empty final response.
- `chat-controller.js` correctly rejects it as `missing_final_transcript`.
- User sees failure with no recognition text.

### 3) Gateway/coordinator contract currently does not normalize empty-final outcomes into explicit speech error codes
- Files:
  - `blazeclaw/BlazeClawMfc/src/core/SpeechTranscriptionCoordinator.cpp`
  - `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- Empty-final is propagated as successful completion rather than explicit native error classification.

Impact:
- Error semantics are discovered only in WebView as a synthetic `missing_final_transcript`, not in native pipeline.

## Fix Plan (Step-by-Step)

1. **Fix WebView request contract for preview calls**
   - Update `chat-controller.js` `transcribeSpeech()` to include `livePreviewOnly` in `transcriptRequest` sent to `speech.transcribe`.
   - Keep final requests explicitly `livePreviewOnly: false`.

2. **Harden native Sherpa finalization semantics**
   - In `SherpaZipformerStreamingEngine::TranscribeStreaming`, when request is final and decoded transcript is empty:
	 - return `ok=false`
	 - set `sessionState.stage=Failed`
	 - set structured error (`no_speech` or `inference_failed`-class code, as appropriate).
   - Preserve existing diagnostics (`sherpaFinalOutcome`, token counters) for troubleshooting.

3. **Normalize and map empty-final errors at coordinator/gateway boundary**
   - Ensure `SpeechTranscriptionCoordinator` and gateway response mapping propagate explicit native error code/message/class for empty-final cases.
   - Avoid emitting `completed` lifecycle state for empty-final responses.

4. **Align WebView error handling with native error codes**
   - In `chat-controller.js`, prefer native error code handling path when final transcript is empty and native supplied an explicit error.
   - Keep `missing_final_transcript` fallback only for legacy/unknown payloads.

5. **Add regression coverage for transport contract and empty-final behavior**
   - Add JS regression test ensuring preview transcribe requests include `livePreviewOnly: true`.
   - Add native test(s) for Sherpa final request producing empty decoded text -> failed result with explicit error code.
   - Add integration-level assertion that final lifecycle does not report `completed` with empty transcript.

6. **Validate with required build and focused speech tests**
   - Build using project-required command:
	 - `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
   - Run focused speech tests and manual scenario:
	 - phrase `请讲一个笑话` with preview on/off
	 - verify final response contains transcript text or explicit native error (not silent empty success).

7. **Verify diagnostics parity and operator visibility**
   - Confirm telemetry includes explicit final outcome/error classification in native logs.
   - Confirm WebView no longer relies on synthetic `missing_final_transcript` for this path except true legacy mismatch.

## Implementation Status

All plan steps (1-7) are implemented in code.

- Step 1 implemented: `speech.transcribe` payload now always includes `livePreviewOnly` from WebView (`web/chat/chat-controller.js`).
- Step 2 implemented: Sherpa final drained requests now fail explicitly when final transcript is empty (`src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`).
- Step 3 implemented: gateway/coordinator now normalize completed-with-empty-transcript into failed semantics with explicit native error mapping (`src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`, `src/core/SpeechTranscriptionCoordinator.cpp`).
- Step 4 implemented: regression coverage added in WebView controller regression suite and native parity tests (`web/chat/chat-controller.js`, `tests/GatewaySpeechPhase56ParityTests.cpp`).
- Step 5 implemented: associated docs updated (this file and speech parity plan notes).
- Step 6 implemented: build/tests executed and recorded in implementation summary.
- Step 7 implemented: diagnostics path now carries explicit native error outcomes and preserves fallback behavior only for legacy/unknown payloads.

## Updated Validation Targets

- Verify preview dispatch payload includes `livePreviewOnly=true`.
- Verify final dispatch payload includes `livePreviewOnly=false`.
- Verify empty final native response with explicit `errorCode` is surfaced as native failure in WebView (not rewritten to `missing_final_transcript`).
- Verify truly empty/no-error legacy final response still maps to `missing_final_transcript` fallback.
- Verify preview empty `completed` responses do not force `failed` stage while recording remains active.
- Verify warmup runs (`speech-warmup-*`) are excluded from transcript-required failure semantics.

## Expected Result After Fix
- For normal speech input (`请讲一个笑话`), final transcript is surfaced.
- If final transcript is truly unavailable, user receives explicit native speech error classification (not successful empty completion).
- No more ambiguous `ok=true` + empty-final payloads causing UI-side `missing_final_transcript` surprises.
