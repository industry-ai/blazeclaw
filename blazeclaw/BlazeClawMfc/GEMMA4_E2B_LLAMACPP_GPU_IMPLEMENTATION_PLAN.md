# Gemma 4 E2B (IT) + llama.cpp Local GPU Implementation Plan

## Goal
Run `gemma-4-E2B-it` locally with `llama.cpp` on GPU in BlazeClawMfc, and make model/runtime selection configurable through `blazeclaw.conf`.

## Target Model Location
- `blazeclaw/BlazeClawMfc/models/google/gemma-4-E2B-it/`

## Implementation Scope
- Add `llama.cpp` runtime path in BlazeClawMfc local model runtime.
- Add GPU runtime controls for `llama.cpp`.
- Add config keys in `blazeclaw.conf` for model selection and runtime tuning.
- Integrate active model/provider routing and settings persistence.
- Validate build and functional chat path.

## Non-Goals (Phase 1)
- Automatic model conversion workflows.
- Multi-GPU sharding/tensor parallel.
- Advanced speculative decoding.

## Proposed `blazeclaw.conf` Configuration Contract
Use explicit keys for `llama.cpp` backend and Gemma model entry.

```ini
# Local runtime provider
chat.localModel.enabled=true
chat.localModel.provider=llama.cpp
chat.localModel.rolloutStage=dev

# Gemma model directory and model artifact
chat.localModel.storageRoot=blazeclaw/BlazeClawMfc/models/google/gemma-4-E2B-it
chat.localModel.modelPath=gemma-4-E2B-it.gguf

# Generation defaults
chat.localModel.maxTokens=256
chat.localModel.temperature=0.2

# llama.cpp runtime controls
chat.localModel.llama.gpuLayers=999
chat.localModel.llama.contextLength=8192
chat.localModel.llama.batchSize=512
chat.localModel.llama.threads=6
chat.localModel.llama.flashAttention=true
chat.localModel.llama.verboseMetrics=true

# Active model routing
chat.activeProvider=local
chat.activeModel=llama/gemma-4-E2B-it
chat.model.enabled.llama/gemma-4-E2B-it=true
```

## Work Plan and Tracking

### Phase 1 — Build & Dependency Integration
- [x] Pin and integrate `llama.cpp` dependency for Windows x64.
- [x] Enable GPU backend build path used by BlazeClawMfc target.
- [x] Link/include integration in `BlazeClawMfc.vcxproj` for Debug/Release.
- [x] Validate with required build command:
  - [x] `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`

Phase 1 build integration notes:
- Added optional `llama.cpp` include/lib/bin wiring in `blazeclaw/BlazeClawMfc/BlazeClawMfc.vcxproj`.
- Added post-build runtime DLL copy target for llama artifacts.
- Added dependency onboarding doc: `blazeclaw/BlazeClawMfc/docs/llamacpp-phase1-build-integration.md`.
- Required Debug x64 `msbuild` validation command completed successfully.

### Phase 2 — Runtime Adapter in LocalModel
- [ ] Add `llama.cpp` runtime adapter in `src/core/runtime/LocalModel`.
- [ ] Implement model lifecycle: load, warmup (optional), unload.
- [ ] Implement generation loop: streaming + non-streaming.
- [ ] Map runtime errors to existing diagnostics/fallback behavior.

### Phase 3 — Config Parsing and Validation
- [ ] Add parser support for `chat.localModel.provider=llama.cpp`.
- [ ] Add parser support for `chat.localModel.llama.*` keys.
- [ ] Resolve model path from `storageRoot + modelPath`.
- [ ] Add validation for missing model file and invalid numeric values.
- [ ] Keep fallback to previous valid model when activation fails.

### Phase 4 — Active Model/Provider Routing
- [ ] Ensure `chat.activeProvider=local` + `chat.activeModel=llama/gemma-4-E2B-it` resolves to llama runtime.
- [ ] Preserve ONNX and remote provider behavior.
- [ ] Persist and restore active model across restart.

### Phase 5 — Settings/UI Integration
- [ ] Expose Gemma llama model entry in settings model list.
- [ ] Persist enable/disable with `chat.model.enabled.*` keys.
- [ ] Display backend label (e.g., `Local (llama.cpp)`) if supported.

### Phase 6 — Functional and Regression Validation
- [ ] Functional: chat response generated from Gemma local model.
- [ ] Functional: streaming output behaves correctly in UI.
- [ ] Negative: invalid GGUF path reports actionable diagnostics.
- [ ] Regression: ONNX local model still works when selected.
- [ ] Regression: DeepSeek/other remote providers unaffected.

## Config File Synchronization Checklist
Current repository has multiple `blazeclaw.conf` copies. Keep keys synchronized:
- [ ] `blazeclaw/blazeclaw.conf`
- [ ] `blazeclaw/BlazeClawMfc/blazeclaw.conf`
- [ ] `blazeclaw/BlazeClawMfc/src/config/blazeclaw.conf`

## Acceptance Criteria
- `llama.cpp` GPU runtime can load and run `gemma-4-E2B-it` from configured path.
- `blazeclaw.conf` fully controls backend/model/runtime knobs without code edits.
- Active provider/model selection persists and routes correctly.
- Required Debug x64 msbuild command passes.
- Existing ONNX/remote paths remain usable.

## Risks and Mitigations
- **GPU backend mismatch** → pin known-good `llama.cpp` commit and build flags.
- **Model memory pressure** → provide conservative defaults and explicit diagnostics.
- **Config drift across duplicated conf files** → enforce synchronization checklist in PR.

## Suggested Execution Order
1. Build integration.
2. Runtime adapter.
3. Config parser + validation.
4. Active model routing.
5. Settings integration.
6. Validation and regression checks.
