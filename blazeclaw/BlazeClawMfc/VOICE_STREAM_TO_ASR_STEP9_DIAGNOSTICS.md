# Voice Stream to ASR Step 9: Diagnostics Expansion

## Goal
Add targeted speech diagnostics so it is immediately clear where a voice transcription stall or shutdown hang is occurring.

## Diagnostics added

### Coordinator (`SpeechTranscriptionCoordinator`)
- `accept.accepted` with `sessionId`, `runId`, `audioPath`
- `accept.rejected_busy_session` and `accept.rejected_busy_run`
- `execute.start` when background runtime transcription begins
- `execute.completed` with terminal stage and latency
- `cancel.requested` with source attribution:
  - `source=api`
  - `source=shutdown`
- `shutdown.summary` with cancelled/cleared counts

### Runtime (`SpeechRecognitionRuntime`)
- `transcribe.request.accepted` with session/run/audio metadata
- `transcribe.preprocessing.completed` with `sampleRate`, `durationMs`, `frames`
- `transcribe.inference.completed` with `latencyMs`
- `transcribe.transcript.completed` with transcript `length`, `language`, `latencyMs`
- `transcribe.cancelled` at each checkpoint:
  - `stage=before_preprocessing`
  - `stage=before_inference`
  - `stage=during_decode`
- `cancel.requested` at runtime cancel entry (`source=coordinator_or_shutdown`)

### Service teardown (`ServiceManager`)
- `speech.shutdown.summary` with final counters and cancelled delta across shutdown
- `native_recording.stop` with stop result payload (`ok`, `audioPath`, `error`)

## Outcome
The speech path now emits enough diagnostics to distinguish stalls across:
- admission/rejection
- file handoff and preprocessing
- ONNX inference and decode
- cancellation origin and checkpoint stage
- shutdown cancellation/drain and recorder-stop cleanup

## Files updated
- `BlazeClawMfc/src/core/SpeechTranscriptionCoordinator.cpp`
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`
- `BlazeClawMfc/src/core/ServiceManager.cpp`
- `BlazeClawMfc/VOICE_STREAM_TO_ASR_WIRING_PLAN.md`
- `docs/README.md`
