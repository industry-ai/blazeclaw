---
name: "Phase 1 Skill Portability Contract Checklist"
about: "Track rollout of Phase 1 contract freeze for OpenClaw -> BlazeClaw skill portability."
title: "[Phase 1][Skills Portability] Contract Freeze Rollout"
labels: ["blazeclaw", "skills", "parity", "documentation"]
assignees: []
---

## Goal

Freeze the **Phase 1 compatibility contract** so OpenClaw skills can be ported to BlazeClaw without editing skill files, and keep all associated docs in sync.

Reference source of truth:

- `blazeclaw/docs/skill/blazeclaw-openclaw-skill-porting-plan.md` (Phase 1 section)

Current implementation state in this branch:

- ✅ Fully implemented (contract baseline + rollout closure evidence recorded).

## Rollout tracking

| Field | Value |
|---|---|
| Owner | @<owner> |
| Status | Done |
| Start Date | 2026-06-23 |
| Target Date | 2026-06-23 |
| Completed Date | 2026-06-23 |
| Last Updated | 2026-06-23 |

## Phase 1 execution checklist

### 1) Freeze compatibility contract surface

- [x] Confirm canonical skill file contract (`name`, `description`, OpenClaw-compatible `metadata`).
- [x] Confirm canonical parser/runtime ownership remains in:
  - `blazeclaw/BlazeClawMfc/src/core/SkillsFrontmatterCompat.*`
  - `blazeclaw/BlazeClawMfc/src/core/MarkdownFrontmatterCompat.*`
  - `blazeclaw/BlazeClawMfc/src/core/SkillsCatalogService.*`

### 2) Freeze frontmatter field/alias semantics

- [x] Document supported frontmatter keys and aliases.
- [x] Document strict vs non-strict frontmatter behavior.
- [x] Confirm no compatibility regression for OpenClaw metadata policy parsing.

### 3) Freeze prompt compatibility boundaries

- [x] Keep OpenClaw-compatible `<available_skills>` body stable.
- [x] Keep BlazeClaw planner context additive/optional only.
- [x] Keep compact fallback semantics stable.

### 4) Freeze command invocation compatibility

- [x] Keep `/skill <name>` resolution stable.
- [x] Keep direct `/<command>` resolution stable.
- [x] Keep `command-dispatch: tool` + `command-tool` semantics stable.
- [x] Keep command sanitization/dedupe behavior stable.

### 5) Freeze source-root/import policy

- [x] Confirm source-root precedence is documented and versioned.
- [x] Confirm `skills-openclaw-original` import lane semantics are documented.

### 6) Enforce no-skill-file-edit policy

- [x] Runtime evolution changes go to adapters/config/runtime code first.
- [x] Any skill-file exception is documented with rationale (security/malformed input remediation only).

### 7) Add/confirm verification gates

- [x] Frontmatter compatibility fixtures/tests updated.
- [x] Eligibility compatibility fixtures/tests updated.
- [x] Prompt-shape compatibility fixtures/tests updated.
- [x] Command invocation compatibility fixtures/tests updated.

## Required docs updates (same PR)

- [x] `blazeclaw/docs/skill/blazeclaw-openclaw-skill-porting-plan.md`
- [x] `blazeclaw/docs/skill/blazeclaw-openclaw-skill-workflow-analysis.md`
- [x] `blazeclaw/docs/skill/skill.md`
- [x] `blazeclaw/docs/reviews/WORKSTREAM_A_STEP7_SPEECH_BRIDGE_COORDINATOR.md`
- [x] `blazeclaw/docs/PROJECT_REVIEW.md` (if present in current branch) - not present in this branch.

## Validation commands

Run from repo root unless noted:

```powershell
# 1) Build (required)
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001

# 2) Optional release compile sanity
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Release /p:Platform=x64 /p:CodePage=65001
```

## Risks / blockers

- [x] Inventory/content drift reviewed and documented in Phase 2 rollout artifacts.
- [x] Extension skill/plugin-root coverage documented with current boundaries.
- [x] Host dependency validation recorded through required build gates.

## Completion criteria

- [x] All Phase 1 checklist items above are complete.
- [x] Required docs are updated and cross-linked.
- [x] Build validation completed.
- [x] Final status row updated to `Done` with `Completed Date`.

## Completion evidence

- Compatibility contract baseline is documented in `blazeclaw/docs/skill/blazeclaw-openclaw-skill-porting-plan.md` (Phase 1 section).
- Parser/runtime ownership is confirmed in:
  - `blazeclaw/BlazeClawMfc/src/core/SkillsFrontmatterCompat.*`
  - `blazeclaw/BlazeClawMfc/src/core/MarkdownFrontmatterCompat.*`
  - `blazeclaw/BlazeClawMfc/src/core/SkillsCatalogService.*`
- Associated docs were synchronized in this rollout closure pass.
- Validation evidence:
  - Debug build: passed (`msbuild "E:\gitRepo\blazeClaw\blazeclaw\BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`).
  - Release compile sanity (optional): attempted; observed `C1041` PDB contention in `BlazeClawMfc.vcxproj` (`vc145.pdb` concurrent write), recorded as non-contract blocker.
