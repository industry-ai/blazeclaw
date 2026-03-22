# BlazeClaw Embedding Models

This folder is the local packaging root for ONNX embedding assets copied to the
build output.

## Layout

Use one subfolder per model:

- `models/embeddings/<model-id>/model.onnx`
- `models/embeddings/<model-id>/tokenizer.json`
- optional metadata (`config.json`, `LICENSE`, checksums)

## Phase 1 baseline

- Primary: `bge-small-en-v1.5`
- Fallback: `all-MiniLM-L6-v2`

## Notes

- Files in this folder are copied to:
  - `bin/<Configuration>/models/embeddings/...`
- ONNX runtime DLLs are copied to:
  - `bin/<Configuration>/`
- Keep large model binaries out of git unless repository policy explicitly
  allows committing model artifacts.
