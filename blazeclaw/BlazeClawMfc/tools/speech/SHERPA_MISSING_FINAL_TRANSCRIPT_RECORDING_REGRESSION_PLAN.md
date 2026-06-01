# Sherpa `missing_final_transcript` Follow-up Regression Fix Plan

## Problem Summary
After the previous fix, clicking **Transcribe** changes the button to `Recording... (click to stop)` and then immediately back to `Transcribe`.

Observed failures:

- Live preview request transitions to `failed` while still recording.
- UI shows `Recognition failed: final speech response completed without transcript text`.
- Runtime startup shows warmup failure:
  - `startup.runtime.status=warmup_failed`
  - `startup.runtime.warmup.error=final transcript unavailable: no_tokens_emitted`

## Root Cause (Code-Based)

### Root cause 1: Empty-transcript failure logic is over-broad in Sherpa engine and affects warmup
- File: `blazeclaw/BlazeClawMfc/src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- Current logic sets `missingFinalTranscript` and returns failed when final transcript is empty.
- This now also applies to internal warmup runs (`speech-warmup-*`), where transcript text should not be required.

Impact:
- Runtime warmup becomes `failed`, setting startup status to `warmup_failed`.

### Root cause 2: Gateway normalization converts any `completed + empty text` to failed, including preview
- File: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- `completedWithoutTranscript` currently triggers for all requests.
- Preview polling requests often return empty interim payloads and should not be treated as terminal failure.

Impact:
- Preview request emits `failed` lifecycle during recording.
- WebView recording state is broken mid-capture.

### Root cause 3: UI button state is derived from speech stage; forced preview `failed` collapses recording mode
- File: `blazeclaw/BlazeClawMfc/web/chat/index.js`
- Button caption logic uses `speechSessionState.stage`.
- When stage becomes `failed`, `isRecordingSpeechStage(...)` is false and caption reverts to `Transcribe`.

Impact:
- User cannot keep recording until explicit stop.

## Fix Plan (Step-by-Step)

1. **Scope native empty-final transcript failure to true final user transcription paths only**
   - In Sherpa engine, keep explicit empty-final failure for final user requests.
   - Exclude internal warmup runs from transcript-required failure semantics (warmup success should depend on runtime execution, not transcript presence).
   - Preserve existing diagnostics (`sherpaFinalOutcome`, decoded counters).

2. **Restrict gateway `completedWithoutTranscript` normalization to final requests only**
   - In speech handler, gate conversion by request intent:
	 - apply only for final transcribe flows,
	 - skip for preview/live polling requests.
   - Use structural request signals already present (`livePreviewOnly`, run/request context), not phrase/path hard-coding.

3. **Keep preview lifecycle non-terminal while recording**
   - Ensure preview empty payloads remain `streaming`/non-terminal during active recording.
   - Do not emit preview `failed` solely because transcript text is empty.

4. **Protect warmup pipeline from transcript policy regressions**
   - Ensure warmup (`speech-warmup-*`) does not surface `final transcript unavailable` as runtime warmup failure.
   - Keep warmup diagnostics observable without converting to blocking runtime status when decode text is empty.

5. **Add regression coverage for this specific failure mode**
   - WebView regression:
	 - while recording, preview empty `completed` payload must not force `failed` stage;
	 - button should remain `Recording... (click to stop)` until explicit stop.
   - Native/gateway parity test:
	 - verify empty transcript normalization applies to final request, not preview request;
	 - verify warmup path is not failed by empty transcript policy.

6. **Update docs and troubleshooting guidance**
   - Update this follow-up plan status after implementation.
   - Update related speech workflow docs with explicit distinction:
	 - preview-empty is non-terminal,
	 - final-empty is terminal failure,
	 - warmup does not require transcript text.

7. **Validate with required build + focused tests + manual run**
   - Build:
	 - `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
   - Focused tests:
	 - speech streaming/realtime tests,
	 - new preview/final/warmup regression cases.
   - Manual verification:
	 - click Transcribe and speak `请讲一个笑话`;
	 - button must stay in recording mode until click-to-stop;
	 - no immediate preview `failed` during active capture;
	 - final result returns transcript or explicit final-only failure.

## Acceptance Criteria

- Recording button no longer flips back to `Transcribe` immediately after start.
- Preview requests do not emit terminal `failed` when transcript text is empty during active recording.
- Final requests still return explicit native failure when final transcript is missing.
- Warmup status is no longer `warmup_failed` solely due to empty decoded transcript.
- Build and focused tests pass; manual scenario is reproducibly fixed.

## Implementation Status

All planned steps (1-7) are implemented.

- Step 1 implemented: Sherpa empty-final transcript failure now excludes warmup runs (`speech-warmup-*`) while preserving final-user failure semantics.
- Step 2 implemented: gateway `completedWithoutTranscript` normalization is now gated to non-preview flows (`!livePreviewOnly`).
- Step 3 implemented: preview empty completed payloads remain non-terminal during active recording.
- Step 4 implemented: warmup transcript policy regression removed by warmup exclusion in Sherpa transcript-required failure logic.
- Step 5 implemented: regression coverage added in WebView controller regressions and native parity tests.
- Step 6 implemented: associated docs updated (this follow-up plan and related speech notes).
- Step 7 implemented: build and focused speech tests executed successfully.

## Validation Evidence (Implemented)

- WebView regression includes case: preview `completed` + empty text does not transition to `failed` while recording.
- Native parity test now asserts:
  - gateway normalization includes `!livePreviewOnly`,
  - Sherpa transcript policy includes warmup exclusion (`speech-warmup-`).
