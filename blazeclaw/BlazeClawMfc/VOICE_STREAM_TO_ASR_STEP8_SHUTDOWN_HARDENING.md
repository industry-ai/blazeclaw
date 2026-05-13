# Voice Stream to ASR Step 8: Shutdown Hardening

## Goal
Ensure speech transcription and recording tear down deterministically so app exit and debug stop do not hang while voice work is in flight.

## What changed
- Added `SpeechTranscriptionCoordinator::Shutdown(...)` to cancel all tracked speech runs, clear coordinator state, and suppress any later completion callbacks.
- Registered a `speech_transcription_shutdown` cleanup step in `ServiceManager` so speech cancellation runs before gateway/runtime teardown completes.
- Registered a `native_recording_stop` cleanup step so any active fallback recorder is stopped before shutdown finishes.
- Kept the existing detached WebView transcription worker model intact so the UI thread still never waits for speech worker completion.

## Shutdown ordering
1. service shutdown begins
2. native recording is stopped if active
3. speech transcription runs are cancelled and cleared
4. gateway host shutdown continues
5. remaining runtime cleanup completes

## Runtime behavior preserved
- `SpeechRecognitionRuntime::Transcribe(...)` still runs off the UI thread.
- Runtime cancellation checkpoints before preprocessing, before inference, and during decode remain in place.
- WAV-file handoff remains the current audio boundary.

## Validation focus
- Close the app during transcription and verify the process exits cleanly.
- Stop debugging during transcription and verify the UI thread does not wait on worker completion.
- Confirm recorder shutdown still writes and closes the WAV artifact deterministically.
