# llama.cpp Model Loading Integration Plan (BlazeClawMfc)

## Objective
Integrate `llama.cpp` as a first-class local inference backend in BlazeClawMfc so GGUF models can be discovered, loaded, selected, and used for chat generation without requiring ONNX runtime for those models.

## Scope
- Native dependency integration for `llama.cpp` (Windows x64, Debug/Release)
- Runtime abstraction updates in `src/core/runtime/LocalModel`
- Model manifest and configuration pipeline updates (`blazeclaw.conf` + model metadata)
- Active model/provider selection integration with existing service flow
- UI exposure in Settings dialog model list
- Validation, diagnostics, and rollout guardrails

## Out of Scope (Initial Iteration)
- Quantization/conversion tooling automation (GGUF conversion pipeline)
- Multi-GPU tensor parallelism
- Speculative decoding and advanced batching
- Cross-platform build targets beyond current Windows MFC target

## Current State Summary
- Local model path currently includes ONNX-focused runtime entries.
- Settings flow already persists model enablement and active model/provider.
- Service manager already tracks active provider/model and supports remote providers.
- This allows adding a new local provider path with minimal disruption if we preserve existing contracts.

## Target End State
1. A `llama.cpp` runtime backend exists behind the local runtime abstraction.
2. `blazeclaw.conf` supports selecting and tuning llama-based models.
3. Settings UI can enable/select llama-backed model entries.
4. Service routing can activate llama-backed models via provider/model IDs.
5. Debug/Release solution builds pass with `msbuild`.
6. Functional chat generation works with at least one reference GGUF model.

## High-Level Architecture

### Provider Identity
- Keep provider namespace explicit, for example:
  - `local` provider with model IDs like `llama/<model-alias>`
  - or `llama` provider with plain model IDs
- Recommendation: use `local` + model IDs prefixed by `llama/` to stay aligned with existing active-provider flow.

### Runtime Layering
- Add `LlamaTextGenerationRuntime` parallel to existing ONNX runtime.
- Keep a thin adapter around `llama.cpp` C API so the rest of codebase stays stable.
- Route generation requests through existing service manager entry points.

### Model Metadata
- Extend model metadata format to include:
  - runtime type (`onnx` / `llama`)
  - gguf file path
  - context size defaults
  - generation defaults (temperature, top_p, max_tokens)
  - optional chat template override

## Detailed Implementation Phases

### Phase 1 — Dependency and Build System Foundation
1. Decide dependency strategy:
   - Git submodule under `third_party/llama.cpp`, or
   - pinned source snapshot in vendor folder.
2. Build `llama.cpp` static library targets for Debug/Release x64.
3. Integrate outputs into `BlazeClawMfc.vcxproj`:
   - include directories
   - library directories
   - additional dependencies
   - runtime library compatibility checks
4. Verify no symbol/link conflicts with existing dependencies.
5. Confirm clean `msbuild` for both configs.

### Phase 2 — Runtime Abstraction Extension
1. Audit current local runtime interfaces in `src/core/runtime/LocalModel`.
2. Introduce runtime-type discriminator (`onnx`, `llama`) where model runtime is resolved.
3. Add `LlamaTextGenerationRuntime.h/.cpp` with responsibilities:
   - context initialization
   - model load/unload lifecycle
   - prompt formatting handoff
   - token generation loop
   - cancellation checks/timeouts
   - align with current Onnx runtime behavior where cancel flags are cleared on all terminal paths (success/error/cancel)
4. Add deterministic error mapping (load failure, OOM, invalid GGUF path, incompatible model).
5. Keep logging/TRACE style consistent with current diagnostics.

### Phase 3 — Configuration and Model Registry
1. Extend config parsing to support llama model definitions:
   - example keys:
     - `chat.model.runtime.<id>=llama`
     - `chat.model.path.<id>=models/chat/<name>.gguf`
     - `chat.model.context.<id>=8192`
2. Add validation during startup:
   - missing file
   - unreadable model
   - invalid numeric tunables
3. Add fallback behavior:
   - if selected llama model invalid, fall back to current default local model and emit diagnostic.
4. Ensure configuration write-back preserves existing keys and comments as much as current pipeline allows.

### Phase 4 — Service Routing and Activation Logic
1. Update model resolution logic so active provider/model can map to llama runtime entries.
2. Ensure `SetActiveChatProvider(provider, model)` activates llama models reliably.
3. Preserve existing behavior for DeepSeek and ONNX models.
4. Add guardrail: if activation fails at runtime, revert to last known-good model in memory and config.
5. Add explicit diagnostics for provider/model mismatch scenarios.

### Phase 5 — Settings Dialog and UX Integration
1. Add llama model entries to `SettingsDialog::LoadModels()` with stable IDs.
2. Ensure enable/disable state persists using existing `chat.model.enabled.<id>` pipeline.
3. Add minimal model source labeling (for example `Local (llama.cpp)`) if list format allows.
4. Preserve default-selection behavior when no model is enabled.
5. Confirm selection persists across app restart.

### Phase 6 — Prompting and Generation Semantics
1. Define baseline chat template strategy:
   - model-provided template when available
   - fallback generic chat template
2. Standardize stop sequences and EOS handling.
3. Normalize generation parameters between ONNX and llama where feasible.
4. Implement streaming/non-streaming compatibility with current UI update flow.
5. Ensure UTF-8 handling remains stable in request/response boundaries.

### Phase 7 — Performance and Resource Controls
1. Add config knobs for llama runtime:
   - `threads`
   - `n_batch`
   - `n_gpu_layers` (future-safe even if initially CPU-only)
   - `context_length`
2. Define safe defaults based on desktop class hardware.
3. Add memory pressure checks and clearer user-facing failure diagnostics.
4. Add optional warm-up load toggle for first-token latency reduction.

### Phase 8 — Validation and QA
1. Build validation:
   - `msbuild blazeclaw\BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64`
   - `msbuild blazeclaw\BlazeClaw.sln /m /p:Configuration=Release /p:Platform=x64`
2. Functional validation:
   - enable llama model in Settings
   - set as active model
   - send chat prompt
   - verify response path end-to-end
3. Failure-path validation:
   - missing GGUF file
   - corrupted GGUF
   - invalid context setting
4. Regression validation for existing ONNX and DeepSeek flows.

### Phase 9 — Documentation and Operational Readiness
1. Add maintainer doc for llama integration:
   - required binaries
   - expected folder layout
   - config keys
2. Add troubleshooting guide:
   - model not loading
   - slow generation
   - out-of-memory
3. Add model onboarding checklist for adding a new GGUF entry.
4. Capture known constraints for first release.

## Proposed File/Area Touch Points
- `blazeclaw/BlazeClawMfc/BlazeClawMfc.vcxproj`
- `blazeclaw/BlazeClawMfc/src/core/runtime/LocalModel/*`
- `blazeclaw/BlazeClawMfc/src/core/ServiceManager.*`
- `blazeclaw/BlazeClawMfc/src/app/SettingsDialog.cpp`
- `blazeclaw/BlazeClawMfc/src/config/*` (or equivalent config parsing area)
- `blazeclaw/third_party/llama.cpp/*` (if vendored)
- `blazeclaw/BlazeClawMfc/README` or dedicated runtime docs

## Milestones and Acceptance Criteria

### Milestone A — Build Integration Complete
- `llama.cpp` compiles and links in Debug/Release.
- No new unresolved externals or runtime CRT mismatches.

### Milestone B — Runtime Functional Path
- One GGUF model can be loaded and used for successful chat generation.
- Active provider/model state persists correctly.

### Milestone C — UI + Config Integration
- User can enable/select llama model from Settings.
- `blazeclaw.conf` reflects selection and tunables.

### Milestone D — Regression Safe
- Existing ONNX and DeepSeek flows remain functional.
- Basic negative-path handling is stable and diagnosable.

## Risk Register
1. **ABI/Build Drift in llama.cpp**
   - Mitigation: pin commit SHA and document update process.
2. **High RAM usage for larger GGUF models**
   - Mitigation: conservative defaults, clear diagnostics, model-size guidance.
3. **Runtime behavior divergence from ONNX flow**
   - Mitigation: centralize parameter normalization and fallback handling.
4. **Config complexity growth**
   - Mitigation: strict key naming and validation with sane defaults.
5. **UI clutter from many model entries**
   - Mitigation: keep curated defaults; defer advanced catalog UI.

## Suggested Execution Order
1. Dependency/build integration
2. Runtime abstraction + llama adapter
3. Service routing and activation
4. Config schema support
5. Settings UI updates
6. Functional and regression validation
7. Documentation and rollout notes

## Rollout Strategy
- Stage 1: hidden/feature-flagged llama provider for internal testing.
- Stage 2: opt-in local llama entries in Settings.
- Stage 3: default-visible llama entries after stability criteria.

## Definition of Done
- Code integrated and builds cleanly via `msbuild` Debug/Release x64.
- llama-backed model can be selected and generates responses.
- Config persistence, fallback behavior, and diagnostics are verified.
- Documentation enables another developer to add and validate a new GGUF model without reverse engineering.