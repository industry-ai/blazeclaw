# Sherpa `no_speech_detected` still surfaced as `inference_failed` — Root Cause & Fix Plan

## Problem Summary
User flow:
- Click `Transcribe`
- Speak `请讲一个笑话`
- No transcript appears
- UI still shows red failure:
  - `speech transcribe inference_failed: final transcript unavailable: no_speech_detected (...)`

This indicates two coupled issues:
1. **Runtime still returns no final transcript** (`no_speech_detected` path is real).
2. **Gateway/UI classification is regressed** (still rendered as `inference_failed` red error instead of no-speech guidance).

## Root-Cause Analysis (Code + Log Correlation)

### 1) No-speech terminal outcome is generated correctly in Sherpa
File:
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`

Observed logic:
- `finalOutcome = "no_speech_detected"` when:
  - `!streamState.speechActive`
  - `streamState.decodedTokenCount == 0`
- Then `missingFinalTranscript` marks result failed with error message:
  - `final transcript unavailable: no_speech_detected`

This matches your runtime behavior.

### 2) Gateway remap only happens when `normalizedErrorCode` is empty
File:
- `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`

Current behavior in `completedWithoutTranscript` block:
- `noSpeechOutcome` is detected from debug info.
- But remap to `no_speech_detected` is currently gated by:
  - `if (normalizedErrorCode.empty()) { ... }`

If native already set `errorCode=inference_failed`, gateway keeps it unchanged.
That exactly explains why UI still shows:
- `speech transcribe inference_failed: ... no_speech_detected`

### 3) UI severity follows gateway error code/class
Files:
- `BlazeClawMfc/web/chat/chat-controller.js`
- `BlazeClawMfc/web/chat/index.js`

Because gateway response still carries `inference_failed`, classifier path remains toast/error style, resulting in red box behavior.

### 4) Capture path still produces no speech content (primary functional failure)
From your logs:
- Preview requests repeatedly complete with no text.
- Final request fails at end with no transcript.

This indicates capture/voice-activity/decode didn’t produce tokens for that run (not a warmup/runtime-load failure).

## Fix Plan (Step-by-Step)

1. **Fix gateway no-speech remap precedence (classification correctness) — highest priority**
   - In `completedWithoutTranscript` normalization, if `noSpeechOutcome == true`, force:
	 - `normalizedErrorCode = "no_speech_detected"`
	 - `transcribe.errorCode = "no_speech_detected"`
   - Do this regardless of prior `inference_failed` value.
   - Keep `inference_failed` only for non-no-speech fault outcomes.

2. **Align session error object code with no-speech semantics**
   - In the same block, ensure `transcribe.sessionState.error` code/message are normalized to no-speech guidance semantics when `noSpeechOutcome` is true.
   - Prevent mixed payload states like `errorCode=inference_failed` + message containing `no_speech_detected`.

3. **Add defensive no-speech normalization fallback in WebView controller**
   - In `chat-controller.js`, when payload indicates:
	 - `errorMessage` contains `no_speech_detected`, or
	 - debug/preflight markers indicate no-speech,
	 - normalize effective code/class to `no_speech_detected` / `status` for rendering.
   - This protects UI behavior if gateway ever drifts again.

4. **Expose and surface capture diagnostics in the response area for triage**
   - Ensure payload carries and UI can display key debug markers for failed final run:
	 - `sherpaChunkEnergy*`
	 - `sherpaVoicedChunkCount`
	 - `sherpaNearZeroSamplePermille`
	 - `sherpaInputHealthIndex`
	 - `captureChannelIndex` / `captureChannelEnergyPermille`
   - Show these in operator diagnostics panel/log line to distinguish:
	 - true silent capture,
	 - weak/near-zero capture,
	 - insufficient voiced segments.

5. **Harden adaptive channel selection for this failure mode**
   - In `VoiceRecorder`, refine early lock conditions:
	 - avoid locking too early on transient noise,
	 - allow channel reselection before finalization if channel energy collapses,
	 - ensure final artifact reflects effective selected channel.
   - Preserve fixed override behavior from config.

6. **Add regressions**
   - Gateway parity:
	 - assert no-speech outcome forces `errorCode=no_speech_detected` even when native starts as `inference_failed`.
   - WebView regression:
	 - final no-speech should render status guidance (non-red blocking behavior),
	 - should not emit hard error toast for this class.

7. **Documentation + validation**
   - Update speech troubleshooting docs with a dedicated section:
	 - `inference_failed` + `...no_speech_detected` mismatch means classification regression,
	 - required triage fields and expected guidance behavior.
   - Validate with required build:
	 - `msbuild "BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
   - Run focused speech parity tests and manual phrase check (`请讲一个笑话`).

## Acceptance Criteria

- Final no-speech runs return/propagate `errorCode=no_speech_detected` (not `inference_failed`).
- WebView displays non-blocking status guidance for no-speech (no red hard-failure card for this class).
- Failure payload contains capture diagnostics sufficient for root-cause triage.
- Build succeeds and focused speech regressions pass.

## Implementation Status (Completed)

Completed Step 1-7 implementation:

1. Gateway no-speech remap precedence now forces `no_speech_detected` when no-speech outcome/message signals are present, regardless of prior `inference_failed`.
2. Session error object semantics are aligned to avoid mixed errorCode/message states.
3. WebView controller adds defensive no-speech normalization fallback (`inferNoSpeechSignal`) for resilient status rendering.
4. Added compact `noSpeechTriage` payload surfacing (energy/voiced/near-zero/health/channel markers) across lifecycle/session/response payloads.
5. Adaptive channel selection in `VoiceRecorder` is hardened with configurable stable-lock and controlled relock-on-collapse behavior.
6. Gateway/native parity and WebView regressions were extended for no-speech precedence and diagnostics markers.
7. Associated docs were updated and validation executed with required build + focused speech test.
