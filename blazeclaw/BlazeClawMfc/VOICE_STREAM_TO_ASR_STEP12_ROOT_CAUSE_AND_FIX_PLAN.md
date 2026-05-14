# Voice Stream to ASR Step 12: Updated Root-Cause Analysis and Fix Plan

## Problem statement
After clicking `Transcribe` and speaking `tell a joke, please`, the transcript becomes garbled (for example: `USTERUSTERå°½...`) and the chat response is contaminated with control markers and replayed prompt content (`<|im_start|>user ...`, repeated Q/A blocks).

## New observed evidence
- Web UI receives malformed transcript text before chat send.
- Runtime startup log still reports speech tokenizer path as `...\vocab.json` (not `tokenizer.json`).
- `localModel.response` contains leaked control/session markers and repeated conversation payload.
- Skills final detail mirrors the polluted assistant output, confirming contamination reached the orchestration path.

## Root-cause analysis

### RC1 (primary): tokenizer decode path is still incompatible with Qwen ASR artifacts
In `SpeechRecognitionRuntime.cpp`, runtime builds token text from `vocab.json` and concatenates raw pieces.

Why this fails:
- Qwen ASR model ships `tokenizer.json` + `added_tokens.json` and requires tokenizer decode rules (byte-level restoration and special token handling).
- `vocab.json` entries contain mojibake-like fragments when decoded naively.

Impact:
- transcript text contains corrupted bytes/characters (`Âł...`, `USTER...`, `å°½...`).

### RC2 (critical): transcript termination boundary is incomplete
Current decode path only treats EOT / `im_end` as terminal. Generated control marker `im_start` is not treated as hard stop for transcript handoff.

Why this fails:
- once `im_start` appears, model can continue generating structured chat template text (`user`, prior turns, synthetic Q/A).

Impact:
- ASR output includes prompt/control payload and multi-turn junk, not just spoken utterance.

### RC3 (secondary): generation path is simplified (decoder_init reused per step)
Runtime repeatedly invokes `decoder_init` for every decode step rather than `decoder_init` + `decoder_step` with carried KV state.

Impact:
- reduced decode stability and higher chance of degenerate/repetitive continuation.

### RC4 (containment gap): no transcript quality/safety gate before `chat.send`
`chat-controller.js` forwards any non-empty transcript text.

Impact:
- malformed/control-contaminated ASR text enters chat pipeline and pollutes LLM interaction.

## Fix plan

## Phase A — Correct tokenizer decode (highest priority)
1. Update runtime to load tokenizer metadata from `tokenizer.json` (and `added_tokens.json` when present) as primary decode source.
2. Implement proper ID-to-string decode using tokenizer rules:
   - byte decoder / byte fallback restoration
   - special token table handling
   - normalized whitespace reconstruction.
3. Keep `vocab.json` decode only as explicit fallback mode and emit diagnostics when fallback is active.

Target files:
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.h`

## Phase B — Enforce strict transcript stop/sanitization boundary
4. Treat these tokens as terminal for ASR transcript assembly: `eot`, `im_end`, `im_start`, and configured special stop tokens from tokenizer metadata.
5. Strip any residual control markers from final transcript (`<|...|>`, role tags, template blocks) before returning payload.
6. Add stop-reason telemetry (`eos`, `special_token`, `timeout`, `max_steps`, `cancelled`).

Target files:
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`

## Phase C — Align decode architecture with model design
7. Load `decoder_step(.int4).onnx` and use:
   - step 0: `decoder_init`
   - step N>0: `decoder_step` + carried decoder state/KV cache.
8. Keep existing cancel/timeout checkpoints and ensure they apply to both init and step decode paths.

Target files:
- `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`

## Phase D — Add downstream guardrail before chat forwarding
9. Add transcript quality gate in web controller before `sendPayload(...)`:
   - block transcript if mojibake ratio/repetition/control-token score exceeds threshold
   - surface speech status error to user instead of forwarding to chat
   - keep raw transcript in diagnostics only.

Target files:
- `BlazeClawMfc/web/chat/chat-controller.js`
- `BlazeClawMfc/web/chat/index.js`

## Phase E — Validation and regression coverage
10. Add focused runtime diagnostics:
- tokenizer mode (`tokenizer_json` / `vocab_fallback`)
- decoded token count + stop reason
- transcript sanitization actions taken.
11. Manual smoke tests:
- utterance `tell a joke, please` -> transcript readable, no control markers, relevant joke response.
- verify no leaked `<|im_start|>` / `<|im_end|>` in final assistant output.
12. Re-run cancel/shutdown smoke to ensure no regression in app exit behavior.

## Acceptance criteria
- ASR transcript is readable UTF-8 natural language.
- Final `speech.transcribe` text contains no control markers or chat-template fragments.
- Garbled transcript is blocked from chat-send path.
- Speaking `tell a joke, please` yields a relevant joke response path.
- App remains responsive and exits cleanly during/after transcription.

## Execution status update
- Phase A: Implemented in runtime (tokenizer.json-first loading, fallback vocab mode, tokenizer diagnostics mode).
- Phase B: Implemented core boundaries (stop on special tokens incl. `im_start` / `im_end`, control-marker sanitization, stop-reason telemetry).
- Phase C: Implemented guarded decoder selection (`decoder_init` first, `decoder_step` preferred on subsequent steps when available).
- Phase D: Implemented frontend transcript quality gate and retry UX state for blocked transcripts.
- Phase E: Runtime diagnostics/build validation completed; manual smoke still required for final behavioral sign-off.
