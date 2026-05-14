# Voice Stream to ASR Step 13: Phase A-E Implementation Notes

## Scope
This document records implementation progress for the Step 12 Phase A-E fix plan and provides validation guidance.

## Implemented changes

### 1) Runtime tokenizer source and decode foundation (Phase A)
Updated `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`:
- Added tokenizer loading priority:
  - primary: `tokenizer.json`
  - fallback: `vocab.json`
- Added runtime tokenizer mode tracking (`tokenizer_json` / `vocab_fallback`).
- Added ByteLevel decode helpers:
  - UTF-8 codepoint extraction
  - ByteLevel char->byte reverse map
  - token piece decode via byte map
- Added special token ID tracking from tokenizer metadata (`added_tokens`) plus baseline speech control IDs.

### 2) Transcript boundary, sanitization, and telemetry (Phase B)
Updated `SpeechRecognitionRuntime.cpp` decode flow:
- Generation now stops on all known special tokens (including `im_start`, `im_end`, `endoftext`).
- Added transcript sanitization to strip residual control/template markers.
- Added decode stop reason telemetry (`timeout`, `eos`, `special_token`, `max_steps`) and tokenizer mode in trace output.

### 3) Decoder runtime path hardening (Phase C)
Updated `SpeechRecognitionRuntime.cpp`:
- Session state still detects and loads `decoder_step(.int4).onnx` when present for diagnostics.
- Runtime decoding is now pinned to `decoder_init` for all steps as a safety rollback.
- Added explicit trace telemetry when a step model is present but intentionally disabled pending real `input_embeds` + KV-cache integration.
- Existing timeout/cancel checkpoints remain active.

### 4) Frontend transcript quality gate (Phase D)
Updated `BlazeClawMfc/web/chat/chat-controller.js`:
- Added transcript quality assessor before `sendPayload(...)`.
- Reject criteria include:
  - control marker leakage (`<|im_start`, `<|im_end`, assistant response markers)
  - excessive repeated-run patterns
  - mojibake-like character pattern ratio
- On rejection, speech session is marked failed with `errorCode=transcript_rejected` and chat forwarding is blocked.

Updated `BlazeClawMfc/web/chat/index.js`:
- Button caption now shows `Transcribe (retry)` after `transcript_rejected` failures.

## Diagnostics and build status (Phase E)
- File-level diagnostics passed for edited runtime/web files.
- Runtime failure surfaces now include decoder strategy and generated-token context for `decoder_failed` / `inference_failed` investigation.
- Solution build succeeded after implementation changes.

## Manual validation checklist
1. Start app and open WebView chat.
2. Click `Transcribe`, record: `tell a joke, please`.
3. Verify:
   - no garbled transcript appears in input/chat
   - no leaked control tokens (`<|im_start|>`, `<|im_end|>`) in chat output
   - if transcript quality is invalid, UI blocks forwarding and shows retry behavior.
4. Repeat with cancellation during transcribing and confirm UI recovery.
5. Close app during/after transcription and confirm clean exit.

## Known limitations / follow-up
- `decoder_step` remains intentionally disabled in live decoding until the runtime implements the model-export-specific `input_embeds` + KV-cache contract.
- `decoder_init`-only decoding is a stabilization fallback and may still require prompt or logits-path tuning after more microphone samples.
- Transcript quality thresholds may require tuning after several real microphone samples.
