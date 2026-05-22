# ASR model switching implementation plan (qwen3-asr-1.7b-onnx / qwen3-asr-0.6b-onnx)

## Goal
Implement user-visible switching between two local STT models:
- `BlazeClawMfc/models/STT/qwen3-asr-1.7b-onnx/`
- `BlazeClawMfc/models/STT/qwen3-asr-0.6b-onnx/`

without changing the ONNX runtime backend contract.

## Progress
- Completed: Step 1 (ASR id<->storageRoot mapping helpers in `SettingsDialog.cpp`)
- Completed: Step 2 (single-select feature-model behavior on load/save)
- Completed: Step 3 (persist selected model mapping into `speech.storageRoot`)
- Completed: Step 4 (manual non-catalog storage root preserved with unselected catalog state)
- Completed: Step 5 (optional `speech.activeModelId` schema + loader + Settings persistence)
- Completed: Step 6 (docs/config comments updated for catalog id + switching behavior)
- Completed: Step 7 (validation run with required command attempt + project fallback build)
- Remaining: none

## Codebase analysis summary

### Current configuration/runtime path flow
- Speech runtime model root is resolved from config keys already consumed by runtime:
  - `speech.storageRoot`
  - `speech.model_path`
- Runtime resolution path is centralized in:
  - `BlazeClawMfc/src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.cpp`
  - `ResolveConfiguredPath(modelPath, storageRoot)` and subsequent ONNX file discovery.
- Config parsing is already present in:
  - `BlazeClawMfc/src/config/ConfigLoader.cpp`
  - (`speech.enabled`, `speech.provider`, `speech.storageRoot`, `speech.model_path`, etc.)

### Current Settings UI state
- `CSettingsDialog::LoadFeatureModels()` shows two ASR entries in `IDC_LIST_MODELS_EX`:
  - `speech/qwen3-asr-1.7b-onnx`
  - `speech/qwen3-asr-0.6b-onnx`

- Save path in `OnOK()` now:
  - normalizes feature selection to single-select,
  - maps selected feature model id to catalog `speech.storageRoot`,
  - persists optional `speech.activeModelId`.
- Manual non-catalog `speech.storageRoot` edits are preserved with no catalog
  model selected.

### Current status
Catalog-based ASR model switching is implemented for 1.7B and 0.6B models,
including persisted model identity and custom-root compatibility behavior.

## Design decision
Use `speech.storageRoot` as the single source of truth for selected ASR model directory.

Rationale:
- Runtime already consumes it with no extra runtime contract changes.
- No new protocol surface is required.
- Keeps model switching data-driven and future-extensible (more local folders later).

## Implementation plan

### Step 1: Add explicit ASR model identity mapping in Settings dialog
File: `BlazeClawMfc/src/app/SettingsDialog.cpp`
- Introduce a local static mapping table from feature model id to default storage root:
  - `speech/qwen3-asr-1.7b-onnx -> BlazeClawMfc/models/STT/qwen3-asr-1.7b-onnx`
  - `speech/qwen3-asr-0.6b-onnx -> BlazeClawMfc/models/STT/qwen3-asr-0.6b-onnx`
- Add helpers:
  - resolve selected feature index from `speech.storageRoot`
  - resolve storage root from selected feature id.

### Step 2: Make feature selection mutually exclusive (radio-like behavior)
File: `BlazeClawMfc/src/app/SettingsDialog.cpp`
- In `LoadFeatureModels()`, set exactly one selected model when `speech.enabled=true`:
  - Prefer match by current `speech.storageRoot`
  - fallback to 1.7B model when no match.
- Enforce single selection on save:
  - if multiple checked, keep first checked and clear the rest (deterministic order)
  - if none checked, set `speech.enabled=false`.

### Step 3: Persist selected model root on save
File: `BlazeClawMfc/src/app/SettingsDialog.cpp`
- In `OnOK()`:
  - derive `selectedFeatureModelId`
  - derive `selectedStorageRoot` from mapping table
  - write `speech.storageRoot=selectedStorageRoot` when a model is selected
  - keep `speech.model_path` editable override behavior unchanged
  - keep `speech.provider=onnx` as current behavior.

### Step 4: Keep manual path edits compatible
File: `BlazeClawMfc/src/app/SettingsDialog.cpp`
- If user edits `ASR Storage Root` manually to a non-catalog path:
  - preserve edited value in `speech.storageRoot`
  - keep closest model checkbox semantics:
	- either uncheck all (catalog mismatch), or
	- select matching catalog item only when exact mapped path matches.
- Recommended behavior: uncheck all catalog models on mismatch to avoid misleading UI state.

### Step 5: Optional config schema enhancement for future model catalogs
Files:
- `BlazeClawMfc/src/config/ConfigModels.h`
- `BlazeClawMfc/src/config/ConfigLoader.cpp`
- `BlazeClawMfc/src/config/blazeclaw.conf`
- (optional) `BlazeClawMfc/blazeclaw.conf`

Add optional key:
- `speech.activeModelId=speech/qwen3-asr-1.7b-onnx|speech/qwen3-asr-0.6b-onnx`

This is optional for current rollout because `speech.storageRoot` is sufficient, but recommended for future catalog growth and analytics clarity.

### Step 6: Documentation updates
Update docs to reflect switching behavior and source of truth:
- `blazeclaw/docs/readme.md`
- `BlazeClawMfc/src/config/blazeclaw.conf` comments
- `BlazeClawMfc/blazeclaw.conf` comments

Document:
- two supported STT folders
- Settings feature list is single-select for ASR model
- `speech.storageRoot` drives runtime model resolution.

### Step 7: Validation plan
1. Build validation:
   - Preferred repo command (if solution exists):
	 - `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
   - Fallback (current workspace):
	 - `msbuild "BlazeClawMfc/BlazeClawMfc.vcxproj" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
2. Manual functional checks:
   - Select 1.7B in Settings -> save -> verify `speech.storageRoot` is 1.7B path.
   - Select 0.6B in Settings -> save -> verify `speech.storageRoot` is 0.6B path.
   - Restart app / re-open settings -> selected model remains consistent.
   - Trigger STT once each selection and verify runtime status reports expected model path.
3. Regression checks:
   - `speech.enabled` toggle behavior unchanged.
   - Existing hotword/runtime-hot keys remain untouched.

## Risks and mitigations
- Risk: checkbox list allows multi-select while feature is conceptually single-select.
  - Mitigation: enforce single-select normalization on save and sync on load.
- Risk: manual storage root edits conflict with catalog checkboxes.
  - Mitigation: mismatch -> no catalog selection, but keep manual path value.
- Risk: stale defaults across config templates.
  - Mitigation: update both template and runtime config comments consistently.

## Out of scope for this iteration
- Automatic benchmark-based dynamic model switching.
- Download/install pipeline for missing STT model folders.
- Non-ONNX speech providers.
