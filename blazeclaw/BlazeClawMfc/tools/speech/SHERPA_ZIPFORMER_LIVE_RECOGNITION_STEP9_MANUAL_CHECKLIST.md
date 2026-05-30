# Sherpa Zipformer Live Recognition Step 9 Manual Checklist

Use this checklist to manually validate the GUI live-recognition fix with real
microphone input. The checklist is intentionally structural: it validates preview
and final orchestration without relying on phrase-specific code paths.

## Shared setup

- Build Debug x64 before manual validation.
- Use the active config file: `BlazeClawMfc/blazeclaw.conf`.
- Restart BlazeClaw after each preview toggle change.
- Use the Sherpa Zipformer STT model configured by `speech.storageRoot`.
- Capture Visual Studio Output logs for each scenario.
- If WebView developer tools are available, capture WebView console diagnostics
  for `[speech-request-trace]` and `[speech-preview-diagnostic]`.
- Record the final visible transcript and whether a chat message was sent.

## Preview toggle

### Disable preview

1. In `BlazeClawMfc/blazeclaw.conf`, uncomment or add:

   `env.BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=false`

2. Restart BlazeClaw.
3. Confirm the WebView header speech status includes `preview=off`.
4. Start recording and confirm the live preview box shows
   `Live preview disabled` with `Preview is off; final transcription will run
   after stop.` The nearby `Abort` button is not the preview status.

### Enable preview

1. In `BlazeClawMfc/blazeclaw.conf`, remove/comment the false override:

   `env.BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=false`

2. Or explicitly set:

   `env.BLAZECLAW_SPEECH_LIVE_PREVIEW_ENABLED=true`

3. Restart BlazeClaw.
4. Confirm the WebView header speech status does not include `preview=off`.
5. Start recording and confirm the live preview box shows normal listening or
   recognition text rather than `Live preview disabled`.

## Required diagnostics to capture

For each scenario, capture the following when present:

- `speech.request.trace` / `[Speech][RequestTrace]`
- `speech.capabilities.get` payload, when available, with
  `streamingPreviewEnabled=false` and `livePreviewToggleEnabled=false` for the
  disabled-preview run
- WebView `[speech-preview-diagnostic]` entry `speech.preview.disabled` with
  `reason=capability_toggle` or `reason=capability_unloaded` if recording starts
  before capabilities are loaded
- `speech.bridge.order` / `[Speech][BridgeOrder]`
- `SpeechTranscriptionCoordinator` `accept.*` and `execute.*` diagnostics
- `[VoiceRecorder][artifact.preview]`
- `[VoiceRecorder][artifact.final]`
- `[SherpaStreaming][final.start]`
- `[SherpaStreaming][final.summary]`
- WebView `[speech-request-trace]`
- WebView `[speech-preview-diagnostic]`

Important evidence markers:

- preview requests use `requestType=preview`, `livePreviewOnly=true`, and a
  `speech-preview-*` run id.
- disabled-preview runs must not emit `requestType=preview` or
  `livePreviewOnly=true` dispatches after recording starts.
- preview polling is fail-closed: if capabilities are not loaded or do not
  explicitly report `streamingPreviewEnabled=true`, preview must stay disabled
  and final transcription must still run after stop.
- preview `completed` or `segment_finalized` lifecycle events for a
  `speech-preview-*` run must not change the button back to `Transcribe`; the
  button should stay `Recording... (click to stop)` until the user explicitly
  stops recording.
- final requests use `requestType=final`, `livePreviewOnly=false`, and a
  `speech-final-*` run id.
- preview artifacts may be open-ended with `sequenceEnd=0`.
- final artifacts must be finite with `sequenceEnd > sequenceStart`.
- final Sherpa summary should show `drained=1`, `remaining=0`, and
  `finalCursor >= sequenceEnd`.
- late preview suppression should appear as `preview_not_active`,
  `stale_preview_update`, or `final_authority_active` when applicable.

## Scenario matrix

| Scenario | Steps | Pass criteria | Result |
| --- | --- | --- | --- |
| Disable preview, speak `请讲一个笑话`, stop | Disable preview, restart, confirm header `preview=off`, confirm preview box `Live preview disabled`, record the phrase, stop. | No preview polling; final request runs from finite artifact; final visible text is not the bad `请听` regression. | Not run |
| Enable preview, speak `请讲一个笑话`, stop | Enable preview, restart, record the phrase, allow at least one preview tick, stop. | Preview may be partial; final request uses `speech-final-*`; final visible text comes from final transcription and is not overwritten by preview. | Not run |
| Enable preview, wait through several preview ticks before stopping | Enable preview, restart, record a phrase, wait until preview text appears or several preview requests complete, then click stop. | Button remains `Recording... (click to stop)` until explicit stop; preview terminal lifecycle events do not end recording; final request runs after stop. | Not run |
| Enable preview, stop quickly | Enable preview, restart, start recording and stop before or during first preview response. | No stale preview overwrites final/empty state; late preview is ignored if it arrives. | Not run |
| Enable preview, speak English | Enable preview, restart, record a short repeatable English phrase, stop. | Preview and final remain coherent; final text comes from final transcription. | Not run |
| Enable preview, cancel recording | Enable preview, restart, start recording, cancel/fail the recording path if available. | Preview is cleared or ignored; no chat send occurs from preview-only text. | Not run |

## Per-scenario notes template

Copy this block once per scenario and fill it from the captured run.

```text
Scenario:
Preview mode: enabled | disabled
Config line:
WebView status:
Live preview box label/text:
Spoken phrase:
Preview run id(s):
Final run id:
Final artifact sequenceStart:
Final artifact sequenceEnd:
Sherpa final drained: yes | no
Sherpa final remaining:
Final visible transcript:
Chat message sent: yes | no
Late preview ignored reason:
Pass/fail:
Notes:
```

## Acceptance criteria

- The reported bad final result `请听` is not reproducible for
  `请讲一个笑话` in the preview-enabled final result.
- Preview-disabled final quality remains comparable to pre-live-recognition
  record-then-transcribe behavior.
- Preview-enabled final quality matches preview-disabled final quality for the
  same speaker, microphone, phrase, and environment.
- Preview text may update while recording but cannot become the authoritative
  final chat message.
- Late preview responses cannot overwrite stopped, transcribing, completed, or
  final-run state.

## If a scenario fails

1. Save the full Visual Studio Output log and WebView console log.
2. Record the exact preview toggle state and WebView speech status.
3. Preserve the final artifact range diagnostics and Sherpa final summary.
4. Continue with Step 10 build/focused-test validation before changing code.
5. Use the failed scenario as the input for a follow-up root-cause fix.
