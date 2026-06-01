# Sherpa `no_speech_detected` Capture Path Root-Cause Fix Plan

## Problem Summary
With the current build, recording flow remains stable, but final transcription for `请讲一个笑话` still fails with:

- `Recognition failed: final transcript unavailable: no_speech_detected`
- UI error card:
  - `speech transcribe inference_failed: final transcript unavailable: no_speech_detected ...`

Runtime/warmup is now healthy (`startup.runtime.status=ready`, warmup succeeded), so the failure shifted from lifecycle regression to capture/decode quality.

## Root-Cause Analysis (Code-Based)

### 1) Final path explicitly reports no speech activity + zero decode tokens
- File: `blazeclaw/BlazeClawMfc/src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`
- Final outcome becomes `no_speech_detected` when:
  - `streamState.speechActive == false`
  - `streamState.decodedTokenCount == 0`

This matches your log behavior (preview polling completes empty repeatedly; final fails with no speech).

### 2) Speech activity gate is energy-threshold only and likely too brittle for current input characteristics
- Same file, loop logic:
  - `ComputeFrameEnergy(chunk)`
  - `frameSpeech = energy >= kSpeechEnergyThreshold`
- If captured waveform level is low (mic gain/input format/channel mismatch), `speechActive` never flips true.

### 3) Capture channel is fixed and not adaptive
- File: `blazeclaw/BlazeClawMfc/src/app/VoiceRecorder.cpp`
- Ring ingestion always uses configured channel index (`ringCaptureChannelIndex`, default effectively channel 0).
- On multi-channel devices, channel 0 can be near-silent while another channel has speech.

### 4) User-facing classification for no-speech is currently too severe
- Current final no-speech becomes `inference_failed` class path in gateway/UI, producing a red hard-error card.
- This obscures actionable guidance (capture/input issue) vs true runtime failure.

### 5) Existing diagnostics are not yet sufficient for direct capture triage
- Logs provide lifecycle and final outcome, but not enough first-class capture evidence (energy stats, channel decision, ring continuity, dropped samples) in a concise per-run diagnostic block.

## Fix Plan (Step-by-Step)

1. **Add per-run capture-quality diagnostics in Sherpa debug output**
   - Emit min/max/avg chunk energy, voiced-chunk ratio, and zero/near-zero sample ratio.
   - Emit ring/read window continuity signals for final request (requested range vs consumed cursor).
   - Keep telemetry for both preview and final requests.

2. **Add explicit no-speech classification path at gateway boundary**
   - When final outcome indicates no speech (or equivalent no-token/no-voice terminal state), map to dedicated `no_speech_detected` code/class instead of generic `inference_failed` severity.
   - Preserve true `inference_failed` for decoder/runtime faults.

3. **Refine WebView error behavior for no-speech**
   - Treat `no_speech_detected` as non-blocking status guidance (retry/mic check), not hard red failure style.
   - Keep current hard-error behavior for runtime/decoder failures.

4. **Implement adaptive capture channel selection in VoiceRecorder**
   - During early recording frames, evaluate per-channel short-window energy.
   - Select the strongest stable speech channel automatically for ring/STT ingest.
   - Keep fixed-channel override behavior when explicitly configured.

5. **Introduce a lightweight preflight health index before final transcribe dispatch**
   - Compute from structural signals already available:
	 - recent chunk energy presence,
	 - ring continuity/readability,
	 - dropped-sample pressure.
   - Attach health index to final request diagnostics and use it to produce targeted guidance.

6. **Add regression coverage**
   - WebView regression: `no_speech_detected` should surface status guidance class and not regress recording/finalization state.
   - Native parity tests: assert no-speech mapping path, capture-quality diagnostics fields, and adaptive-channel marker presence.

7. **Update docs and validate**
   - Update related speech troubleshooting docs with:
	 - no-speech triage checklist,
	 - adaptive-channel behavior,
	 - guidance for mixed CUDA stack warnings vs capture issues.
   - Validate with required build:
	 - `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
   - Run focused speech tests and manual phrase verification (`请讲一个笑话`).

## Acceptance Criteria

- `no_speech_detected` failures are diagnosable with direct capture metrics (not opaque).
- No-speech user feedback becomes actionable status guidance rather than generic hard inference failure.
- Adaptive channel selection reduces false no-speech outcomes on multi-channel devices.
- Build and focused speech tests pass; manual verification confirms improved recognition or clear capture-rooted guidance.

## Implementation Status (Completed)

Completed implementation for Step 1-7:

1. Added capture-quality diagnostics contract fields and gateway debug payload surfacing.
2. Implemented Sherpa runtime capture metrics + input health index emission.
3. Added explicit gateway no-speech classification and retry guidance profile.
4. Implemented adaptive capture channel selection in `VoiceRecorder` with fixed-channel override.
5. Added lightweight preflight health index before final transcribe dispatch and response attachment.
6. Extended regression/parity coverage for no-speech classification and adaptive capture markers.
7. Updated related docs and validated with required `msbuild` command.
