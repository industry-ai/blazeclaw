# Sherpa `no_speech_detected` in WebView mode — Fallback Recorder Root-Cause Plan

## Problem Summary
In WebView-first mode, pressing **Transcribe** and speaking `请讲一个笑话` still ends with:

- `speech transcribe no_speech_detected: final transcript unavailable: no_speech_detected`
- Triage shows fully silent capture:
  - `health=45, energyAvg=0, voiced=0, nearZero=1000, ch=0, chEnergy=0`

Runtime load/warmup is healthy, so the remaining failure is in capture path selection/configuration, not model boot.

## Root-Cause Analysis (Code + Log Evidence)

### 1) Capture stream is populated, but content is effectively silence
From your logs:

- final stream range is non-empty (`sequenceStart=0 sequenceEnd=56289`),
- but triage is all-silence (`energyAvg=0`, `voiced=0`, `nearZero=1000`, `captureChannelEnergy=0`).

This indicates audio frames are flowing, but the selected recorder input/channel path is not speech-bearing.

### 2) WebView mode bypasses `CChatView` recorder path and uses gateway fallback recorder
Relevant code:

- `GatewayHost::StartNativeRecording()`
  - tries `main->GetActiveChatView()` first,
  - when null, uses fallback recorder (`fallback.recorder`).
- In WebView-first UI, active view is `CBlazeClawMFCView`, not `CChatView`, so fallback path is used.

### 3) Fallback recorder is initialized with default `VoiceRecorderConfig` and no explicit input-device binding
Relevant code:

- `fallback.recorder.Initialize(main->GetSafeHwnd())` (default config only).
- `CVoiceRecorder::SetInputDevice(...)` exists but is not called by fallback path.
- No explicit bridge from current speech config/UI-selected input device to fallback recorder startup.

Impact: fallback path can open a valid but wrong/quiet capture endpoint; this matches non-empty sequence + zero-energy triage.

### 4) Channel adaptation cannot recover if source device itself is silent
Even with adaptive channel logic in `VoiceRecorder`, if all channels from the bound device are near-zero, output remains no-speech. Current triage (`ch=0`, `chEnergy=0`) is consistent with this condition.

## Fix Plan (Step-by-Step)

1. **Unify recorder configuration between WebView and native chat paths**
   - Introduce one resolver that builds `VoiceRecorderConfig` from active speech config/runtime snapshot.
   - Use it for both `CChatView` recorder initialization and gateway fallback recorder initialization.

2. **Plumb explicit input-device selection into fallback recorder startup**
   - Add gateway/runtime-level setting retrieval for speech input device id/index.
   - Call `fallback.recorder.SetInputDevice(...)` before `StartRecording(...)` when configured.
   - Keep safe default behavior if no device is configured.

3. **Add startup/recording diagnostics for actual bound capture device**
   - Emit concise `[Chat]` lines when recording starts:
	 - selected device index/name,
	 - recorder channel count,
	 - effective capture-channel policy.
   - This must be present for both WebView fallback and `CChatView` path.

4. **Add pre-recording capture sanity probe for fallback path**
   - During first chunks, compute a short RMS/near-zero probe.
   - If probe remains silent across initial window, emit targeted warning before final transcribe.
   - Keep non-blocking UX; only enrich guidance.

5. **Tighten no-speech guidance for fallback-device mismatch**
   - If stream range is non-empty and triage is full-silence (`energyAvg==0`, `nearZero~1000`, `chEnergy==0`), return device-selection-first guidance.
   - Distinguish this from decode/runtime-empty guidance.

6. **Regression coverage**
   - Native parity test: assert gateway fallback path contains device-binding/config-application markers.
   - WebView regression: assert no-speech full-silence scenario emits device-bound diagnostics and device-focused guidance.

7. **Docs update + validation**
   - Update STT troubleshooting docs with a dedicated “WebView fallback recorder device mismatch” branch.
   - Add triage interpretation rule for `energyAvg=0 + nearZero=1000 + non-empty sequence range`.
   - Validate with required build:
	 - `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
   - Run focused speech regressions and manual phrase verification (`请讲一个笑话`).

## Acceptance Criteria

- WebView recording path uses the same recorder config/device-selection semantics as native path.
- Logs clearly show the actual capture device bound for each run.
- `no_speech_detected` full-silence cases provide device-selection-first guidance.
- Manual retry with correct microphone device yields non-zero capture triage and transcript output for the target phrase.

## Implementation Status (Completed)

1. Unify recorder configuration between WebView and native chat paths

- Added shared resolver `BuildVoiceRecorderConfigFromSpeechConfig(...)` in `VoiceRecorder.cpp/.h`.
- Added recorder-related speech config fields and parsing:
  - `speech.input_device_index`
  - `speech.recorder.*` capture/adaptive knobs
- Applied same resolved `VoiceRecorderConfig` in:
  - `CChatView` recorder initialization
  - gateway fallback recorder initialization (`GatewayHost::StartNativeRecording`).

2. Explicit input-device selection in fallback startup

- Fallback path now reads configured speech input device index and applies:
  - `fallback.recorder.SetInputDevice(...)` when index >= 0.
- `CChatView` path now applies the same configured input device behavior.

3. Startup/recording diagnostics for bound capture device

- Added `[Chat] speech.capture.binding` emission for both paths:
  - `path=chat_view`
  - `path=fallback_webview`
- Diagnostics include:
  - input device index/name
  - recorder channels
  - fixed/adaptive channel policy
  - configured/selected channel and selected channel energy.

4. Pre-recording capture sanity probe for fallback path

- Added lightweight probe metrics in `VoiceRecorderTelemetry`:
  - `captureProbeSampleCount`
  - `captureProbeNearZeroSamplePermille`
  - `captureProbeRmsPermille`
  - `captureProbeWeakSignal`
- Probe is computed on sequence reads and surfaced via fallback warning line:
  - `[Chat] speech.capture.warning - path=fallback_webview weakSignal=true ...`

5. Tightened no-speech guidance for fallback-device mismatch

- Gateway no-speech guidance now has dedicated branch for
  - non-empty capture range + full-silence triage signature.
- Guidance now prioritizes recording device/input source checks before runtime decode guidance.

6. Regression coverage

- Native parity (`GatewaySpeechPhase56ParityTests.cpp`) expanded with assertions for:
  - fallback binding/warning markers
  - shared recorder resolver marker
  - full-silence guidance string marker
  - capture probe marker.
- WebView regression (`chat-controller.js`) expanded with full-silence non-empty-range fixture and device-focused guidance assertion.

7. Docs + validation

- Updated `docs/STT/STT_codes.md` with dedicated WebView fallback mismatch branch and triage rule.
- Added new recorder/input-device keys to `BlazeClawMfc/blazeclaw.conf`.
- Build and focused test execution commands were run during validation.