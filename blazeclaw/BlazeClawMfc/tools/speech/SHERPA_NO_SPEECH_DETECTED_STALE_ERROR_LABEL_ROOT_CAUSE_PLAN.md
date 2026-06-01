# Sherpa no-speech still shown as `inference_failed` label — Root Cause & Fix Plan

## Problem Summary
Current behavior after speaking `请讲一个笑话`:

- Final failure text shown in chat is still:
  - `speech transcribe inference_failed: final transcript unavailable: no_speech_detected (...)`
- Runtime trace shows final request completes and then lifecycle stage becomes `failed`.
- No recognition text appears.

This means classification/labeling is still inconsistent even when message content already indicates `no_speech_detected`.

## Root-Cause Analysis (Code-Based)

### 1) Stale error-code variable is used for user-facing message
File:
- `BlazeClawMfc/web/chat/chat-controller.js`

In final response handling:
- `sessionErrorCode` is captured early from `state.speechSessionState.errorCode`.
- Then defensive normalization may update `state.speechSessionState.errorCode` to `no_speech_detected`.
- But later message/trace logic still uses the old local variable `sessionErrorCode`.

Result:
- UI message keeps old `inference_failed` label even when state was normalized to `no_speech_detected`.

### 2) Message branch condition also uses stale code
Same file:
- `if (!transcriptText && sessionErrorCode) { ... }`

This condition and downstream string formatting should use the normalized effective code, not the pre-normalization snapshot.

### 3) Capture path still appears speech-empty
Trace pattern confirms:
- multiple preview rounds with no text
- final fails with no transcript

So there are two layers:
- **primary functional layer**: no speech decoded from capture path.
- **secondary UX layer**: stale error label leaks `inference_failed` string.

## Fix Plan (Step-by-Step)

1. **Fix effective error-code source in WebView final-failure path**
   - In `chat-controller.js`, compute `effectiveSessionErrorCode` *after* defensive normalization.
   - Replace stale `sessionErrorCode` usage in:
	 - branch condition,
	 - trace payload,
	 - user-facing message string.

2. **Unify classification + message formatting on normalized code**
   - Ensure `classifySpeechError(...)` and message prefix use the same normalized effective code.
   - Prevent mixed output like `inference_failed` + `...no_speech_detected`.

3. **Harden defensive no-speech normalization contract**
   - Keep `inferNoSpeechSignal(...)` path.
   - Guarantee that when no-speech is inferred, all of these are synchronized:
	 - `errorCode`
	 - `errorClass`
	 - `retryGuidance`
	 - message prefix code.

4. **Expose final no-speech triage fields in operator diagnostics**
   - Keep/extend no-speech trace payload to include:
	 - `noSpeechTriage`
	 - effective error code
	 - effective class
   - Make it clear whether classification came from native payload or WebView defensive normalization.

5. **Add targeted WebView regression case for stale-label bug**
   - Reproduce payload with:
	 - initial `errorCode = inference_failed`
	 - `errorMessage` containing `no_speech_detected`
   - Assert final snapshot/message label uses `no_speech_detected` (not `inference_failed`).

6. **Add native/parity guard for no-speech mapping precedence**
   - Verify gateway no-speech precedence markers remain present.
   - Verify no-speech triage payload marker is still emitted.

7. **Docs + validation**
   - Update STT troubleshooting doc with explicit symptom:
	 - `inference_failed` prefix + `...no_speech_detected` message means stale label regression.
   - Validate with required build:
	 - `msbuild "BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
   - Run focused speech regression tests and manual phrase verification (`请讲一个笑话`).

## Acceptance Criteria

- No-speech final UI label is consistently `no_speech_detected`.
- No mixed label/message pair (`inference_failed` + `...no_speech_detected`) appears.
- Regression tests cover stale-label scenario.
- Build and focused speech tests pass.

## Implementation Status (Completed)

Completed Step 1-7 implementation:

1. WebView final-failure path now computes and uses post-normalization `effectiveSessionErrorCode`.
2. Classification and message prefix both use the same normalized effective code.
3. Defensive no-speech normalization now synchronizes retryability/retry strategy with no-speech status semantics.
4. Operator diagnostics include effective code/class and `noSpeechSemanticForced` marker with triage payload.
5. Added targeted stale-label regression assertion for `speech transcribe no_speech_detected:` prefix.
6. Extended parity guards for no-speech precedence assignment and diagnostics marker continuity.
7. Updated STT troubleshooting docs and validated with required build + focused speech regression test.
