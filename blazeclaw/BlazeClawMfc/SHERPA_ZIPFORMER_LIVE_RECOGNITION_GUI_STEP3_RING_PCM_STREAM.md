# Sherpa Zipformer Live Recognition GUI Step 3 - Ring PCM Stream Artifact

## Status
Completed.

## Scope
This step ensures live preview speech-recognition requests use the active ring-buffer PCM stream instead of waiting for the final WAV file.

## Files Updated
- `src/app/VoiceRecorder.cpp`
- `src/gateway/GatewayHost.cpp`
- `src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_PLAN.md`

## Implementation Summary

### Live recording artifacts are open-ended
`CVoiceRecorder::BuildStreamingAudioArtifact()` now distinguishes live recording artifacts from stopped/final artifacts.

While recording:
- `handoffMode` is `PcmStream`
- `streamId` is `voice_recorder`
- `mimeType` is `audio/pcm`
- `container` is `pcm_s16le`
- sample rate, channel count, bit depth, frame samples, and sequence start are populated
- `sequenceEnd` is `0`, which means open-ended/live stream
- `durationMs` still reflects currently available ring-buffer samples when any exist
- an artifact can be returned immediately after recording starts, even before the first captured sample

After recording stops or in deterministic ingest tests:
- `sequenceEnd` remains finite and represents the latest committed ring-buffer sequence
- final WAV fallback behavior is preserved

### Gateway artifact validation accepts live open-ended streams
`GatewayHost::ResolveNativeRecordingArtifact(...)` now accepts a valid PCM stream artifact when either:
- it has a finite range: `sequenceEnd > sequenceStart`, or
- it is open-ended/live: `sequenceEnd == 0`

The gateway still rejects artifacts that do not have:
- `handoffMode == PcmStream`
- non-empty `streamId`
- either a finite or open-ended range

This allows `gateway.speech.startRecording` to return a usable live artifact before the final WAV path is produced.

### Runtime infers streaming input from the PCM artifact
`SpeechRecognitionRuntime::Transcribe(...)` now uses the provided PCM stream artifact when inferring `SpeechStreamingInputContract` for Sherpa Zipformer.

For PCM stream artifacts:
- `streamId` comes from `audioArtifact.streamId` instead of assuming only `voice_recorder`
- `sampleRate`, `channels`, and `bitsPerSample` come from the artifact when present
- `sequenceStart` is clamped to the oldest available ring-buffer sequence
- `sequenceEnd == 0` is preserved for open-ended live preview requests
- finite `sequenceEnd` is clamped to the latest available ring-buffer sequence

Design consequence:
- live preview requests can be sent with only `audioArtifact`, without requiring `audioPath`
- the Sherpa streaming engine sees `sequenceEnd == 0` for live preview and treats it as non-final
- final/stopped requests still use a finite sequence range and can fall back to WAV behavior

## Current Data Flow

```text
web/chat/index.js startLiveSpeechPoll(...)
  -> controller.transcribeSpeech({ audioArtifact, livePreviewOnly: true, runId })
  -> CBlazeClawMFCView async speech.transcribe dispatch
  -> GatewayHost.Handlers.Runtime.SpeechRecognition.cpp refreshes ResolveNativeRecordingArtifact(...)
  -> GatewayHost::ResolveNativeRecordingArtifact(...) returns live PcmStream artifact
  -> SpeechRecognitionRuntime::Transcribe(...) infers open-ended SpeechStreamingInputContract
  -> SherpaZipformerStreamingEngine reads samples by streamId from StreamingAudioSourceRegistry
```

## Acceptance Criteria Results
- Live preview requests can use the ring-buffer PCM artifact before the final WAV file exists.
- `gateway.speech.startRecording` can return a valid `PcmStream` artifact immediately after recording starts.
- Live preview artifacts use `sequenceEnd == 0` so Sherpa treats the request as live/non-final.
- Stopped/final artifacts still use a finite `sequenceEnd`, preserving final transcription and WAV fallback behavior.
- Runtime streaming input inference now follows the artifact stream id and audio metadata instead of ignoring the artifact.

## Follow-up Notes for Step 4
Step 4 should promote non-final streaming segments from `SpeechTranscriptionCoordinator::Execute(...)` so live partial text from Sherpa reaches `speech.lifecycle` updates consistently.
