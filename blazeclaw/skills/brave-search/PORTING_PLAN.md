# Brave Search Skill Porting Plan (OpenClaw -> BlazeClaw)

## Goal
Fully port `openclaw/skills/brave-search/` into `blazeclaw/skills/brave-search/` and ensure the skill is discoverable, callable, and usable from BlazeClaw chat flows without any OpenClaw runtime dependency.

## Scope
- Source skill: `openclaw/skills/brave-search/`
- Target skill: `blazeclaw/skills/brave-search/`
- BlazeClaw runtime integration points:
  - local skill discovery and display (`CSkillView`, `gateway.skills.list`, local filesystem fallback)
  - runtime tool catalog + execution (`gateway.tools.list`, `gateway.tools.catalog`, tool manifest/contracts)
  - chat integration path (skill selection in view + runtime tool invocation in chat orchestration)

## Non-Goals
- Porting unrelated OpenClaw skills in this iteration.
- Reworking all chat orchestration logic beyond required brave-search integration.
- Introducing a BlazeClaw dependency on OpenClaw runtime modules.

## Source Inventory (OpenClaw)
Port these artifacts as the baseline:
- `SKILL.md`
- `_meta.json`
- `package.json`
- `search.js`
- `content.js`
- `.clawhub/origin.json` (optional metadata; keep only if BlazeClaw needs it)

## Target Deliverables (BlazeClaw)
1. `blazeclaw/skills/brave-search/SKILL.md`
2. `blazeclaw/skills/brave-search/_meta.json` (if still meaningful for BlazeClaw)
3. `blazeclaw/skills/brave-search/package.json`
4. `blazeclaw/skills/brave-search/scripts/search.js`
5. `blazeclaw/skills/brave-search/scripts/content.js`
6. `blazeclaw/skills/brave-search/tool-manifest.json`
7. `blazeclaw/skills/brave-search/tool-contracts.json`
8. `blazeclaw/skills/brave-search/config.html` (optional, for API key and behavior config UX)
9. `blazeclaw/skills/brave-search/TOOL_SURFACE.md` (recommended usage and examples)

## Architecture Decisions to Lock Before Implementation
1. **Brave API mode**
   - Decide between:
     - HTML scraping behavior currently in `search.js`, or
     - official Brave Search API mode keyed by `BRAVE_API_KEY`.
   - Recommended: API-first implementation with explicit fallback policy.

2. **Config model**
   - Define where API key and defaults live (env file vs app config vs skill-local config).
   - Keep behavior consistent with BlazeClaw skill conventions and security practices.

3. **Tool namespace and IDs**
   - Use stable namespace, e.g. `brave_search`.
   - Define tool IDs (example):
     - `brave_search.search.web`
     - `brave_search.fetch.content`

## Implementation Phases

### Phase 1 - Baseline skill scaffold in BlazeClaw
- Create `blazeclaw/skills/brave-search/` structure.
- Copy and normalize OpenClaw skill docs/scripts into BlazeClaw layout.
- Move executables under `scripts/` to align with BlazeClaw tool runtime conventions.
- Ensure Node script entrypoints are compatible with BlazeClaw runtime invocation.

### Phase 2 - Tool surface definition
- Author `tool-manifest.json` with stable runtime mapping (`node-cli` entries).
- Author `tool-contracts.json` for each exposed tool, including:
  - required query/url fields
  - optional result count and content extraction flags
  - strict `additionalProperties` handling
- Validate naming consistency between manifest IDs and contract keys.

### Phase 3 - Runtime registration and discovery verification
- Verify skill appears in local implementation discovery (`implemented` category).
- Verify runtime tool registry exposes brave-search tools via:
  - `gateway.tools.list`
  - fallback `gateway.tools.catalog`
- Verify dedup behavior with existing registered skills in `CSkillView` tree.

### Phase 4 - Chat usability integration
- Validate skill selection event flow from `CSkillView` to chat view.
- Ensure brave-search tool IDs are callable from chat runtime orchestration path.
- Confirm task-delta entries include tool name, args/result payloads, and status transitions for brave-search invocations.
- Confirm status output and final assistant response include tool-call outcomes.

### Phase 5 - Configuration and safety hardening
- Add/verify environment variable handling for Brave credentials.
- Add request timeout, network failure handling, and deterministic error codes.
- Add output size limits and truncation policy to avoid UI flooding.
- Add URL/query sanitization and basic input guardrails.

### Phase 6 - Documentation and contributor guidance
- Update `blazeclaw/skills/readme.md` with brave-search entry.
- Add usage examples for chat and direct tool calls.
- Document setup (`npm ci`), required env vars, and troubleshooting.

## Phase 1 Execution Results (Completed)

### Implemented scaffold
- Created `blazeclaw/skills/brave-search/` baseline port scaffold.
- Ported metadata/docs:
  - `SKILL.md`
  - `_meta.json`
  - `package.json`
  - `.clawhub/origin.json`
- Ported scripts into BlazeClaw runtime-aligned layout:
  - `scripts/search.js`
  - `scripts/content.js`

### Normalization applied
- Updated usage/help references to `scripts/search.js` and
  `scripts/content.js` paths.
- Preserved source behavior while relocating script entrypoints into
  `scripts/`.

### Associated docs updated
- Updated `blazeclaw/skills/readme.md` to list `brave-search` under
  implemented local skills and identify the current state as
  Phase 1 scaffold complete.

## Phase 2 Execution Results (Completed)

### Tool IDs and namespace finalized
- Namespace: `brave_search`
- Tool IDs:
  - `brave_search.search.web`
  - `brave_search.fetch.content`

### Tool artifacts implemented
- Added `tool-manifest.json` with stable node-cli runtime mappings.
- Added `tool-contracts.json` with strict JSON contracts:
  - required `query` for web search
  - optional `count` and `content` flags for search
  - required `url` for content extraction
- Added `TOOL_SURFACE.md` with contract and usage examples.

### Associated docs updated
- Updated `SKILL.md` with tool surface section and artifact references.
- Updated `blazeclaw/skills/readme.md` to reflect Phase 2 status and
  include manifest/contracts/tool-surface docs in key files.

## Phase 3 Execution Results (Completed)

### Runtime registration path implemented
- Added `GatewayToolRegistry::LoadSkillToolsFromDirectory(...)` to parse
  local skill `tool-manifest.json` files and register declared tools into
  runtime tool catalogs.
- Wired gateway startup to load skill manifests from:
  - `blazeclaw/skills`
  - `skills`

### Runtime discovery behavior
- `gateway.tools.list` now includes tools declared by local skill manifests,
  including brave-search tool IDs.
- `gateway.tools.catalog` also includes local skill tool entries.

### Associated docs updated
- Updated `TOOL_SURFACE.md` with Phase 3 runtime discovery notes and
  endpoint verification guidance.
- Updated `blazeclaw/skills/readme.md` brave-search status to Phase 3.

## Validation Plan

### 1) Skill asset integrity
- Confirm all required files exist under `blazeclaw/skills/brave-search/`.
- Confirm scripts are executable by configured runtime command.

### 2) Build validation
- Run:
  - `msbuild blazeclaw/BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64`

### 3) Runtime/API validation
- Verify `gateway.skills.list` includes brave-search skill metadata.
- Verify `gateway.tools.list` or `gateway.tools.catalog` includes brave-search tools.
- Execute tool preview/call checks:
  - `gateway.tools.call.preview`
  - `gateway.tools.call`

### 4) Chat integration validation
- In UI, select brave-search skill from skill tree.
- Send chat prompts that trigger search/content tools.
- Verify:
  - tool invocation occurs
  - task deltas are emitted
  - assistant output contains grounded results
  - failures are surfaced with actionable error text

### 5) Regression checks
- Ensure existing skills (e.g., `imap-smtp-email`) remain discoverable/callable.
- Ensure no OpenClaw path/runtime is required at execution time.

## Risks and Mitigations
- **Risk:** brittle HTML scraping due upstream DOM changes.
  - **Mitigation:** prefer official API mode and keep scraping behind optional fallback.
- **Risk:** missing/invalid API key causes silent failures.
  - **Mitigation:** explicit preflight validation with clear error messages.
- **Risk:** chat orchestration does not select brave-search tools consistently.
  - **Mitigation:** add deterministic tool metadata and clear tool descriptions/examples.
- **Risk:** large page extraction output degrades UX.
  - **Mitigation:** enforce max output length and structured truncation.

## Definition of Done
- Brave-search skill exists under `blazeclaw/skills/brave-search/` with complete skill + tool artifacts.
- Skill is visible in BlazeClaw skill view and runtime tool catalog.
- Chat can trigger brave-search tools and receive usable results.
- `msbuild blazeclaw/BlazeClaw.sln` passes.
- No runtime dependency on OpenClaw remains for brave-search behavior.

## Port Completeness Check (Latest Audit)

### Result
- **Not fully ported yet**.

### Completed
- **Phase 1:** Completed
- **Phase 2:** Completed
- **Phase 3:** Completed

### Remaining
- **Phase 4 (chat usability integration):** Not completed
  - Brave-search tools are discoverable in runtime catalogs, but no
    brave-search runtime executor is registered for tool-call execution.
- **Phase 5 (configuration and safety hardening):** Not completed
  - Brave credential preflight and runtime hardening tasks are not yet
    implemented for brave-search execution path.
- **Phase 6 (final docs/usage hardening):** Partially completed
  - Core docs are updated, but final completion depends on Phase 4/5 closure.
