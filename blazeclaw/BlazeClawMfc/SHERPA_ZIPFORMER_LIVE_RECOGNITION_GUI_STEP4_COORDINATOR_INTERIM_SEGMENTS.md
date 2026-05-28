# Sherpa Zipformer Live Recognition GUI Step 4 - Coordinator Interim Segments

## Status
Completed.

## Scope
This step updates `SpeechTranscriptionCoordinator::Execute(...)` so streaming transcription results with interim `SpeechTranscriptSegment` data are promoted into execution-state callbacks instead of only surfacing finalized segments.

## Files Updated
- `src/core/SpeechTranscriptionCoordinator.cpp`
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_PLAN.md`

## Implementation Summary

### Previous behavior
Before this step, the coordinator emitted a segment-specific lifecycle callback only when all of the following were true:
- the request was a streaming request
- `result.sessionState.segment` existed
- `result.sessionState.segment->final` was `true`

That meant non-final Sherpa partial text could be present in the runtime result but would not be promoted as a coordinator execution update.

### New behavior
`SpeechTranscriptionCoordinator::Execute(...)` now handles every streaming result with `result.sessionState.segment`.

For any streaming segment:
- the coordinator builds an updated `SpeechExecutionState` through `BuildState(...)`
- `segment` is copied into the tracked execution state
- `transcriptText` is copied from `result.sessionState.transcriptText` or `result.text`
- audio artifact, streaming input, language, latency, and error fields continue to flow through `BuildState(...)`
- the execution update callback is emitted immediately through the existing callback path

Stage mapping:
- `segment.final == false` -> `SpeechExecutionStage::Streaming`
- `segment.final == true` -> `SpeechExecutionStage::SegmentFinalized`

Final segment behavior remains compatible with the previous lifecycle:
- emit `SegmentFinalized`
- then emit `Stopped`
- then preserve the existing terminal completion callback

### Callback flow
The existing callback path is unchanged:

```text
SpeechTranscriptionCoordinator::Execute(...)
  -> m_executionUpdateCallback(updatedExecutionState)
  -> GatewayHostBindingCoordinator callback
  -> GatewayHost::NotifySpeechExecutionUpdate(state)
  -> telemetry event gateway.speech.execution.update
```

Step 5 can build on this by ensuring the gateway/WebView lifecycle payload carries full segment data to the GUI.

## Acceptance Criteria Results
- Every streaming result containing a segment can now produce a coordinator callback.
- Non-final segments are distinguishable because their execution stage is `Streaming` and their segment has `final == false`.
- Final segments remain distinguishable because their execution stage is `SegmentFinalized` and their segment has `final == true`.
- Duplicate suppression is left to later gateway/UI logic; this step preserves every segment callback opportunity.
- Existing terminal completion callback behavior is preserved.

## Follow-up Notes for Step 5
Step 5 should include segment text, final flag, and sequence in gateway lifecycle payloads so the `CBlazeClawMFCView` / WebView speech UI can render interim text from these coordinator updates.
