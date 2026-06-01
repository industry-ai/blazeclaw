# Sherpa no-speech persists after label fix — Capture Pipeline Root-Cause Plan

## Problem Summary
Current behavior now correctly labels failures as no-speech:

- `speech transcribe no_speech_detected: final transcript unavailable: no_speech_detected (...)`

So the stale-label classification regression is fixed. But recognition still fails for `请讲一个笑话` with no transcript output.

## Root-Cause Analysis (Code + Logs)

### 1) UX/classification layer is no longer the blocker
- Error now surfaces as `no_speech_detected` (expected status class behavior).
- This confirms previous gateway/WebView code-path normalization is active.

### 2) Capture/decoding layer still reports no speech activity for final run
From logs:
- Multiple preview dispatch/complete cycles return no text.
- Final run transitions to `failed` with no transcript.

This is consistent with native Sherpa finalization path where:
- no speech activity and no decoded tokens produce `no_speech_detected`.

### 3) Speech activity gate is still a fixed threshold decision
File:
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp`

Observed:
- `frameSpeech = energy >= kSpeechEnergyThreshold`
- if not crossed across chunks, `speechActive` never meaningfully supports final transcript path.

### 4) Current traces in user logs do not expose capture-quality metrics inline
The shared logs contain request/lifecycle ordering but not direct values for:
- chunk energy profile
- voiced chunk count
- near-zero ratio
- selected capture channel and energy

Without these fields visible in standard output, operator triage remains blind.

### 5) Adaptive channel logic exists, but no evidence yet that selected channel is speech-bearing for this run
Current flow uses PCM stream (`voice_recorder`) and final finite range is present.
The remaining likely failures are:
- selected capture channel not carrying voice,
- near-silent effective levels,
- threshold mismatch for current device/input profile.

## Fix Plan (Step-by-Step)

1. **Expose no-speech triage metrics in standard chat logs (not only internal payload) — highest priority**
   - Emit one concise diagnostics line per final failed run containing:
	 - `sherpaChunkEnergyMin/Max/AvgPermille`
	 - `sherpaVoicedChunkCount`
	 - `sherpaNearZeroSamplePermille`
	 - `sherpaInputHealthIndex`
	 - `captureChannelIndex`
	 - `captureChannelEnergyPermille`
   - Make it visible in the same `[Chat]` trace stream user already shares.

2. **Add final no-speech triage block to WebView response surface**
   - For final no-speech responses, append compact triage details to status text/diagnostic pane.
   - Keep non-blocking status style (no hard error toast path).

3. **Harden speech-activity decision with adaptive floor for low-level speech**
   - Keep base threshold but allow adaptive thresholding from observed chunk-energy distribution.
   - Avoid false positives by requiring minimal voiced consistency before marking speech active.

4. **Refine adaptive capture channel behavior for short utterances**
   - Improve lock/relock strategy for short runs (`请讲一个笑话` class of utterances):
	 - delay hard lock until sufficient evidence,
	 - permit one late reselection before finalization when locked-channel energy remains floor-level.
   - Keep fixed override semantics untouched.

5. **Add pre-final guardrail messaging based on measured capture quality**
   - If no-speech and health metrics show weak capture path, return targeted guidance:
	 - mic level/input channel/device selection prompt.
   - If capture looks healthy but decode empty, return model/runtime-focused guidance.

6. **Expand regression coverage**
   - WebView regression:
	 - no-speech final path must include effective triage payload/log fields.
   - Native/parity regression:
	 - assert adaptive threshold/channel markers and final no-speech diagnostics emission markers remain present.

7. **Docs + validation**
   - Update STT troubleshooting docs with:
	 - mandatory triage fields for no-speech,
	 - interpretation decision tree (capture-weak vs capture-healthy/decode-empty),
	 - short-utterance channel/threshold notes.
   - Validate with required build:
	 - `msbuild "BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
   - Run focused speech regression tests and manual phrase verification (`请讲一个笑话`).

## Acceptance Criteria

- Shared `[Chat]` logs include actionable no-speech triage metrics per failed final run.
- Users can distinguish capture-path weakness from decode/runtime-empty outcomes.
- Short-utterance capture robustness improves (fewer false no-speech outcomes).
- Build and focused speech tests pass.

## Implementation Status (Completed)

1. Exposed no-speech triage metrics in shared `[Chat]` stream:

- `BlazeClawMfc/src/app/BlazeClawMFCView.cpp` now emits `speech.no_speech.triage` for final failed runs with mandatory triage fields.

2. Added final no-speech triage block to WebView response surface:

- `BlazeClawMfc/web/chat/index.js` appends compact triage details in failed `no_speech_detected` status text.

3. Hardened Sherpa speech-activity decision:

- `BlazeClawMfc/src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.cpp/.h` adds adaptive low-level floor and voiced-streak consistency gating before speech activation.

4. Refined adaptive capture channel behavior for short utterances:

- `BlazeClawMfc/src/app/VoiceRecorder.cpp/.h` adds short-run lock path and one-time late reselection when locked-channel energy remains floor-level.

5. Added pre-final guardrail messaging based on measured capture quality:

- `BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp` now distinguishes weak-capture guidance from healthy-capture/decode-empty guidance.

6. Expanded regression coverage:

- `BlazeClawMfc/tests/GatewaySpeechPhase56ParityTests.cpp` asserts triage emission, adaptive threshold/channel markers, and guardrail strings.
- `BlazeClawMfc/web/chat/chat-controller.js` regression fixture now includes and validates `noSpeechTriage` payload retention.

7. Docs + validation updates:

- `docs/STT/STT_codes.md` updated with mandatory triage fields, interpretation decision tree, and short-utterance notes.
- Required solution build executed successfully using:
  - `msbuild "BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
- Focused parity test build/run attempted:
  - `BlazeClawMfc.Tests.vcxproj` currently fails in this environment on missing include `kaldi-native-fbank/csrc/online-feature.h`.
  - WebView no-speech regression coverage was still expanded in source.
