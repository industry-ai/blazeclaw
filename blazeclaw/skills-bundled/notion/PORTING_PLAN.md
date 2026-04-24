# Notion Skill Full Porting Plan (OpenClaw -> BlazeClaw)

## Objective
Fully port and stabilize the Notion skill configuration experience so that selecting `notion` from the BlazeClaw Skill Browser always shows usable configuration inputs (especially `NOTION_API_KEY`) and the skill runs successfully end-to-end in chat/tool flows.

## Current Symptom
- Clicking Notion in the Skill Browser opens a config page with no usable input fields.
- Without `NOTION_API_KEY` input/save support, Notion tool execution cannot authenticate.

## Suspected Root-Cause Areas
1. **Config page path resolution mismatch**
   - `notion/config.html` is in `blazeclaw/skills-bundled/notion/`.
   - Current resolver path logic appears to prioritize `blazeclaw/skills/<skill>/config.html` and may miss `skills-bundled`.
2. **Generated fallback form depends on metadata projection**
   - If dedicated `config.html` is not found, fallback form fields rely on `requiresEnv`/`primaryEnv` in selection payload.
   - Any projection gap for these fields causes empty/insufficient form controls.
3. **Skill Browser payload source inconsistency**
   - Different tree categories (`registered`, `implemented`, `runtime-derived`) may pass different payload richness.
   - Notion selection may route through a path that does not include full config hints.

## Scope
- Source reference:
  - `openclaw/skills/notion/`
- Target runtime:
  - `blazeclaw/skills-bundled/notion/`
- Integration points:
  - Skill browser selection payload construction and tab opening.
  - Config URL resolution + generated fallback behavior.
  - WebView2 bridge load/save/validate channels.
  - Gateway/runtime refresh after save.

## Non-Goals
- Re-architecting all skill configuration UX patterns in one pass.
- Porting unrelated skills unless required by shared infra fixes.

## Porting Phases

### Phase 1 - Reproduce and Trace the Selection-to-Config Pipeline
- Reproduce with a clean run and record:
  - Selected skill key.
  - Selected payload fields (`primaryEnv`, `requiresEnv`, `configPathHints`).
  - Whether dedicated `config.html` or generated fallback was used.
- Confirm if Notion is selected from `registered`, `implemented`, or runtime-derived category.
- Capture deterministic logs for this path to avoid regressions.

Deliverable:
- One short diagnostics note in repo documenting observed runtime route and failing branch.

### Phase 2 - Fix Config Page Discovery Parity for Bundled Skills
- Extend config resolver logic to search all valid skill roots in parity order:
  - `skills-bundled`
  - `skills`
  - optionally `skills-openclaw-original` (read-only parity surface)
- Keep deterministic precedence and no ambiguous duplicate picks.
- Ensure notion specifically resolves to:
  - `blazeclaw/skills-bundled/notion/config.html`

Deliverable:
- Resolver update with explicit test coverage for bundled root resolution.

### Phase 3 - Normalize Skill Selection Payload for Config Generation
- Ensure every clickable skill node that can open config includes canonical fields:
  - `skillKey`
  - `primaryEnv`
  - `requiresEnv[]`
  - `requiresConfig[]` (if any)
  - `configPathHints[]`
- Eliminate category-specific payload degradation for bundled skills.
- Guarantee generated fallback has enough information to render at least one credential input.

Deliverable:
- Unified payload builder/helper used by Skill Browser tree nodes.

### Phase 4 - Harden Generated Fallback Config Form
- Keep fallback form usable even when metadata is partial:
  - If `primaryEnv` exists, always render it first.
  - If both `primaryEnv` and `requiresEnv` are missing but skill is known configurable, show a guided empty-state with explicit error.
- Ensure save/ready/reload channels work with both dedicated and generated pages:
  - `blazeclaw.skill.config.ready`
  - `blazeclaw.skill.config.save`
  - `blazeclaw.skill.config.loaded/saved/error`

Deliverable:
- Robust fallback page behavior with deterministic field rendering.

### Phase 5 - Validate Notion Persistence and Runtime Consumption
- Confirm save path and load path are consistent for Notion env:
  - read existing `.env`
  - save updated values
  - reload view shows masked/restored value behavior as designed
- Confirm `gateway.skills.refresh` updates runtime state after save.
- Execute Notion tool call and verify auth success path with configured key.

Deliverable:
- Verified manual test evidence for config save + successful Notion call.

### Phase 6 - Regression Tests and Safety Nets
- Add focused tests for:
  - config resolver root precedence including `skills-bundled`.
  - selection payload completeness across tree categories.
  - generated config page field derivation from `primaryEnv/requiresEnv`.
  - bridge message save/load roundtrip for skill config.
- Add one fixture for a bundled configurable skill (Notion) to prevent silent regressions.

Deliverable:
- Automated test cases covering the previously broken flow.

### Phase 7 - Documentation and Operator Guidance
- Update skill docs/readme with Notion config path and expected UI behavior.
- Add troubleshooting section:
  - "No fields shown in config page"
  - "Config saved but tool still unauthorized"
- Document canonical env source of truth and migration behavior if legacy paths are detected.

Deliverable:
- Updated docs with direct troubleshooting steps.

## Acceptance Criteria (Definition of Done)
- Selecting Notion in Skill Browser always opens a page with editable `NOTION_API_KEY`.
- Save persists to canonical Notion skill config and reload reflects saved value.
- Runtime refresh occurs after save and Notion tool can execute authenticated requests.
- Behavior is consistent regardless of Skill Browser category/source.
- Bundled skill config discovery is covered by regression tests.

## Validation Checklist
- Build:
  - `msbuild blazeclaw/BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64`
- Runtime/API:
  - `gateway.skills.list` includes Notion `primaryEnv`/`requiresEnv`.
  - `gateway.tools.list` shows Notion runtime tool IDs.
- UI:
  - Skill Browser -> Notion opens config with inputs.
  - Save -> success banner/status + persisted value on reload.
- End-to-end:
  - Chat/tool invocation writes/reads via Notion successfully (no missing credential error).

## Risks and Mitigations
- **Risk:** Fixes hardcode Notion-only behavior.
  - **Mitigation:** Implement root and payload fixes as generic skill-config infrastructure.
- **Risk:** Multiple skill roots can introduce ambiguous resolution.
  - **Mitigation:** enforce deterministic precedence and log selected path.
- **Risk:** Sensitive values leak in diagnostics.
  - **Mitigation:** keep redaction for `token/key/secret/password` fields in status output.

## Execution Order Recommendation
1. Phase 1 (trace)  
2. Phase 2 (resolver parity)  
3. Phase 3 (payload normalization)  
4. Phase 4 (fallback hardening)  
5. Phase 5 (runtime verification)  
6. Phase 6 (tests)  
7. Phase 7 (docs)

## Phase Execution Status

### Phase 1 - Reproduce and Trace the Selection-to-Config Pipeline (Completed)
- Added a deterministic diagnostics note:
  - `blazeclaw/skills-bundled/notion/PHASE1_SELECTION_CONFIG_TRACE.md`
- Recorded end-to-end routing path and the previously failing branch.
- Added runtime route status logging in view layer:
  - `skills.config.route ... mode=dedicated|generated`

### Phase 2 - Fix Config Page Discovery Parity for Bundled Skills (Completed)
- Updated config lookup to search skill roots in deterministic order:
  1. `skills-bundled`
  2. `skills`
  3. `skills-openclaw-original`
- Applied both workspace-relative and nested `blazeclaw/...` root variants.
- Result: Notion now resolves dedicated page from:
  - `blazeclaw/skills-bundled/notion/config.html`

### Phase 3 - Normalize Skill Selection Payload for Config Generation (Completed)
- Added canonical payload normalization for all Skill Browser node categories:
  - `skillKey`
  - `primaryEnv`
  - `requiresEnv[]`
  - `requiresConfig[]`
  - `configPathHints[]`
- Runtime-derived/openclaw/filesystem fallback nodes now reuse catalog payload when available, otherwise emit canonical empty defaults.
- Result: generated config fallback always receives stable field contract.

