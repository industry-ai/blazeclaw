# Voice Stream to ASR Step 11: Root-Cause Analysis and Fix Plan

## Problem statement
After microphone transcription, the UI receives garbled text (`ÂłÂłÂł...`) instead of the spoken sentence, and the chat response becomes unrelated/default (`Hello! How can I help you today?`).

## Reproduced evidence
- WebView shows transcript-like garbage before chat send.
- Runtime logs show normal speech startup and transcription entry, but resulting user message content is invalid.
- `localModel.response` includes control-style residue (`<|im_end|>`, `[assistant_response]`), indicating downstream prompt/response contamination when bad input is forwarded.

## Root-cause analysis

### RC1 (primary): ASR token decoding is implemented with an incompatible tokenizer path
Current runtime decoding in `SpeechRecognitionRuntime.cpp`:
- loads `vocab.json` and builds a direct `id -> token string` map
- reconstructs text by raw string concatenation with only a simple `▁ -> space` replacement

This is incompatible with Qwen ASR tokenizer behavior:
- model directory includes `tokenizer.json` and `added_tokens.json`, but runtime ignores them
- `vocab.json` entries include byte-level/mojibake token strings (observed in model vocab), which require tokenizer decoder rules, not naive concatenation

Impact:
- decoded text becomes mojibake (`Âł...`) even when token IDs may be valid.

### RC2 (secondary): decode execution path is simplified and likely degrades output quality
Runtime uses `decoder_init` repeatedly for each generation step and does not use `decoder_step`/KV-cache progression.

Impact:
- generation quality/stability can degrade, increasing repetitive or malformed token streams.
- may amplify non-linguistic outputs and role/control token leakage.

### RC3 (downstream hygiene): transcript quality gate is missing before chat forwarding
`chat-controller.js` forwards any non-empty transcript text to `chat.send`.

Impact:
- garbled ASR output is treated as valid user input and sent to LLM, causing unrelated/default responses.

## Fix plan

### Phase A — Correct tokenizer decode (must-fix first)
1. Update ASR runtime tokenizer loading to prefer `tokenizer.json` (+ `added_tokens.json` if present) over raw `vocab.json` string decode.
2. Implement proper ID-to-text decode for Qwen tokenizer format:
   - support byte-decoder mapping / byte-fallback restoration
   - apply special-token filtering via tokenizer metadata (not only prefix check)
   - normalize whitespace using tokenizer rules instead of ad-hoc replacement.
3. Keep `vocab.json` path only as explicit fallback for legacy models and flag fallback mode in diagnostics.

Target files:
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.h`

### Phase B — Align decode loop with model architecture
4. Extend runtime session state to load/use `decoder_step(.int4).onnx` when available.
5. Refactor generation loop:
   - first step with `decoder_init`
   - subsequent steps with `decoder_step` and carried decoder state/KV inputs/outputs.
6. Keep current timeout/cancel checkpoints and add per-step telemetry (`decode.step`, `decode.stop_reason`).

Target files:
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`

### Phase C — Add transcript quality guardrail before chat send
7. Add ASR transcript quality validation before `sendPayload(...)`:
   - reject/hold transcript if high mojibake ratio or suspicious repetition pattern
   - surface a user-visible speech status error and keep text out of chat pipeline
   - preserve raw transcript in diagnostics for troubleshooting.

Target files:
- `BlazeClawMfc/web/chat/chat-controller.js`
- `BlazeClawMfc/web/chat/index.js`

### Phase D — Validation and regression checks
8. Add focused runtime diagnostics:
   - tokenizer source mode (`tokenizer_json` vs `vocab_fallback`)
   - decode stop reason (`eos`, `timeout`, `max_steps`, `cancelled`)
   - transcript quality score and rejection reason (if blocked).
9. Re-run manual smoke:
   - utterance: `tell a joke, please`
   - expect transcript close to utterance, no mojibake, no control-token residue
   - verify assistant reply is contextually relevant.
10. Re-run shutdown/cancel smoke to ensure no regressions in exit behavior.

## Acceptance criteria
- Transcribed user text is readable UTF-8 natural language (no `Âł...` mojibake).
- `speech.transcribe` payload text excludes role/control artifacts.
- Chat receives only validated transcript text.
- The utterance `tell a joke, please` produces a relevant joke response path.
- App still exits cleanly during/after speech activity.

## Notes
- Keep runtime path resolution robust for current working directory differences.
- Do not rely on UI-main-window-only logging assumptions during runtime processing.