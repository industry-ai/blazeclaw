# Sherpa Zipformer Live Recognition GUI Step 5 - Gateway Segment Payloads

## Status
Completed.

## Scope
This step ensures gateway speech lifecycle surfaces carry segment text, final state, and sequence metadata so live recognition updates can be rendered by `CBlazeClawMFCView` / WebView consumers.

## Files Updated
- `src/gateway/GatewayHost.Handlers.Runtime.SpeechRecognition.cpp`
- `src/core/GatewayHostBindingCoordinator.cpp`
- `SHERPA_ZIPFORMER_LIVE_RECOGNITION_GUI_PLAN.md`

## Implementation Summary

### speech.transcribe response payloads
`speech.transcribe` responses now include normalized segment data in these places:
- top-level `segment`
- `speechSession.segment`
- `executionState.segment`

The payload also includes top-level lifecycle fields:
- `stage`
- `audioPath`
- existing top-level `text`, `language`, `sessionId`, `runId`, and `latencyMs`

This makes preview responses directly consumable by lifecycle normalization while preserving the existing `speechSession` shape.

### Busy/in-flight responses
When a speech request is rejected because an execution is already active, the busy response now includes:
- top-level `stage`
- top-level `audioPath`
- top-level `segment`
- `speechSession.segment`
- `executionState.segment`

This allows an existing interim segment to survive duplicate preview calls or busy-session responses.

### Gateway lifecycle telemetry
The `gateway.speech.lifecycle` telemetry payload now includes:
- `text`
- structured `segment`

The `gateway.speech.segment` telemetry payload now includes segment `text` in addition to `final` and `sequence`.

### Coordinator execution-update fanout
The `gateway.speech.execution.update` telemetry payload emitted from `GatewayHostBindingCoordinator` now includes:
- `text`
- structured `segment`

This means callbacks promoted by Step 4 preserve interim segment metadata through the coordinator fanout path.

### CBlazeClawMFCView lifecycle bridge compatibility
`CBlazeClawMFCView::BuildSpeechLifecyclePayloadFromTranscribeResponse(...)` already reads `speechSession.segment` and includes it in emitted `speech.lifecycle` payloads.

Because Step 5 now guarantees `speechSession.segment` is present and normalized when a segment exists, `speech.lifecycle` events emitted after `speech.transcribe` completion carry interim/final segment metadata without requiring a new bridge contract.

## Segment Payload Shape

```json
{
  "stage": "streaming",
  "sessionId": "...",
  "runId": "...",
  "text": "latest partial transcript",
  "segment": {
	"text": "latest partial transcript",
	"final": false,
	"sequence": 3
  },
  "latencyMs": 123,
  "audioPath": "...",
  "language": "..."
}
```

## Acceptance Criteria Results
- `speech.lifecycle` events emitted from `CBlazeClawMFCView` can carry interim segment text through `speechSession.segment`.
- `speech.transcribe` preview responses carry the same normalized segment object at top level, under `speechSession`, and under `executionState`.
- Busy/in-flight responses preserve segment data when available.
- Gateway lifecycle and segment telemetry include segment text and metadata.
- Coordinator execution update fanout telemetry includes segment text and metadata.
- Final response compatibility is preserved because existing `speechSession`, `speechArtifact`, `transcriptInjection`, and top-level `text` fields remain present.

## Follow-up Notes for Step 6
Step 6 should render the `speechSessionState.segmentText` / `speechSessionState.text` values in the WebView GUI as a dedicated live transcript preview area near the composer or voice button.
