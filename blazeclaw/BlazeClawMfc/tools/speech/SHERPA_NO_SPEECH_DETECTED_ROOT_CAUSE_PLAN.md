# Sherpa `no_speech_detected` Root-Cause Fix Plan

## Problem Summary
User records `请讲一个笑话`, but no transcript is produced.

Observed result:

- `Recognition failed: final transcript unavailable: no_speech_detected`
- UI shows error toast/status:
  - `speech transcribe inference_failed: final transcript unavailable: no_speech_detected ...`

## Root-Cause Analysis (Code-Based)

### 1) Native final outcome is explicitly `no_speech_detected`
- File: `blazeclaw/BlazeClawMfc/src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- Logic sets `finalOutcome = "no_speech_detected"` when:
  - `streamState.speechActive == false`
  - `streamState.decodedTokenCount == 0`

This means the engine consumed the final audio range, but detected no speech activity and emitted zero decode tokens.

### 2) Speech-activity signal is based on fixed energy threshold and can miss low-level/incorrect capture
- File: `blazeclaw/BlazeClawMfc/src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- `speechActive` is driven by `ComputeFrameEnergy(chunk)` and `kSpeechEnergyThreshold` comparison.

If captured PCM is near-silent (or from a wrong channel/input path), speech activity never flips true.

### 3) Current diagnostics are insufficient to distinguish capture silence vs decoder/runtime issue
- Current logs show lifecycle ordering and final outcome, but do not expose enough per-run capture-energy/ring-health evidence in standard operator output for quick diagnosis.

### 4) CUDA runtime alignment risk is currently warning-only
- Startup logs report `status=mixed` CUDA/cuDNN stack alignment and recommendation to align before production.
- This can contribute to decode instability/no-token outcomes but currently does not influence runtime policy at transcription decision time.

### 5) Error mapping treats `no_speech_detected` path as generic `inference_failed`
- User-facing behavior is a red error-style message, which is too severe for benign no-speech/low-capture scenarios and obscures actionable guidance.

## Fix Plan (Step-by-Step)

1. **Add explicit per-run capture/decode diagnostics for no-speech triage**
   - Extend speech debug snapshot with:
	 - chunk energy stats (min/max/avg),
	 - non-trivial sample ratio,
	 - ring window start/end and dropped-sample signal,
	 - final decoded token counters and final outcome.
   - Ensure diagnostics are emitted for both preview and final requests with clear request type tags.

2. **Implement speech-input preflight health index before final transcribe dispatch**
   - Compute a lightweight health index from available structural signals (energy presence, readable sample window continuity, capture readiness).
   - If health indicates likely silent capture, mark as `no_speech`-class status with actionable guidance instead of hard inference failure.

3. **Harden capture-path robustness (channel/level aware)**
   - Add adaptive capture-channel selection support for multi-channel input (auto-select highest-energy channel at start).
   - Keep config override support for fixed channel index.
   - Record selected channel and measured start-window energy in diagnostics.

4. **Add CUDA dependency preflight gating policy profile for speech decode**
   - Use runtime dependency/alignment health signals to produce a preflight score.
   - When decode risk is high (mixed-stack risk + repeated no-token outcomes), apply configured policy profile to avoid unstable decode path for that run.
   - Keep policy configurable in config and diagnostic output.

5. **Refine error classification and UI behavior for `no_speech_detected`**
   - Distinguish `no_speech_detected` from `inference_failed` in native error code mapping.
   - In WebView/controller, classify `no_speech` as status/retry guidance (non-blocking) rather than red hard-error style.
   - Preserve `inference_failed` severity for true runtime/decoder failures.

6. **Add regression and parity coverage for the failure mode**
   - WebView regression:
	 - no-speech final response should surface non-blocking guidance class,
	 - must not regress recording/finalization state handling.
   - Native parity tests:
	 - no-speech outcome classification path,
	 - capture-health diagnostics presence,
	 - policy-profile branching markers.

7. **Update docs + validate build/tests + manual scenario**
   - Update speech implementation/workflow markdowns with:
	 - no-speech diagnostic interpretation,
	 - health-index/preflight policy behavior,
	 - troubleshooting steps for capture path and CUDA alignment.
   - Validate with required build:
	 - `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
   - Run focused speech tests and manual phrase validation (`请讲一个笑话`).

## Acceptance Criteria

- Final no-transcript cases are diagnostically actionable (capture vs decode risk is distinguishable).
- `no_speech_detected` is surfaced as a dedicated no-speech class with clear retry guidance.
- Recording/finalization state remains stable; no lifecycle regressions.
- CUDA risk policy behavior is visible and configurable.
- Build and focused speech tests pass; manual scenario has deterministic troubleshooting evidence.

## Follow-up Status

Implementation now proceeds via
`tools/speech/SHERPA_NO_SPEECH_DETECTED_CAPTURE_ROOT_CAUSE_PLAN.md` and has
completed Step 1-7 for capture-quality diagnostics, gateway/WebView no-speech
classification, adaptive recorder channel selection, preflight health index,
regression/parity coverage, and docs/build validation.
