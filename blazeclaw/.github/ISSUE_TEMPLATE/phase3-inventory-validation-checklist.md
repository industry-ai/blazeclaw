---
name: "Phase 3 Inventory Validation Checklist"
about: "Track rollout of Phase 3 inventory validation reporting for OpenClaw -> BlazeClaw skill roots."
title: "[Phase 3][Skills Inventory Validation] Reporting Rollout"
labels: ["blazeclaw", "skills", "parity", "documentation"]
assignees: []
---

## Goal

Implement **Phase 3** inventory validation reporting as an audit-only pipeline, then synchronize all associated docs.

Reference source of truth:

- `blazeclaw/docs/skill/blazeclaw-openclaw-skill-porting-plan.md` (Phase 3 section)

Current implementation state in this branch:

- ✅ Fully implemented (runtime validator + tests + rollout docs updated).

## Rollout tracking

| Field | Value |
|---|---|
| Owner | @<owner> |
| Status | Done |
| Start Date | 2026-06-23 |
| Target Date | 2026-06-23 |
| Completed Date | 2026-06-23 |
| Last Updated | 2026-06-23 |

## Phase 3 execution checklist

### 1) Freeze report contract

- [x] Report schema sections are defined.
- [x] Deterministic ordering and reproducibility rules are defined.
- [x] Newline normalization (`\r\n`/`\n`) drift policy is defined.

### 2) Implement inventory collectors

- [x] OpenClaw bundled skill collector implemented.
- [x] BlazeClaw bundled skill collector implemented.
- [x] Extension/plugin root collector implemented.
- [x] Common skill candidate collector implemented.

### 3) Implement diff and drift analyzers

- [x] Missing-upstream analyzer implemented.
- [x] Extra-curated analyzer implemented.
- [x] Newline-normalized `SKILL.md` drift analyzer implemented.
- [x] Stable hash/path metadata included in drift rows.

### 4) Implement extension reachability analyzer

- [x] Extension skill enumeration implemented.
- [x] Reachability checks against plugin roots implemented.
- [x] Unreachable extension reason reporting implemented.

### 5) Implement frontmatter validation analyzer

- [x] Invalid frontmatter detection implemented.
- [x] Oversized frontmatter/file detection implemented.
- [x] Concise per-file issue reason reporting implemented.

### 6) Add report invocation path

- [x] Runtime report generator implemented (`SkillsInventoryValidationService`).
- [x] Reproducible invocation script added (`tools/Invoke-SkillsInventoryValidationPhase3.ps1`).
- [x] Report is audit-only (no automatic rewrite).

### 7) Add/confirm verification gates

- [x] Tests cover missing/extra/drift/reachability/frontmatter categories.
- [x] Tests verify report section presence.
- [x] Deterministic sorted output behavior is validated.

## Required docs updates (same PR)

- [x] `blazeclaw/docs/skill/blazeclaw-openclaw-skill-porting-plan.md`
- [x] `blazeclaw/docs/skill/blazeclaw-openclaw-skill-workflow-analysis.md`
- [x] `blazeclaw/docs/skill/skill.md`
- [x] `blazeclaw/docs/reviews/WORKSTREAM_A_STEP7_SPEECH_BRIDGE_COORDINATOR.md`
- [x] `blazeclaw/docs/PROJECT_REVIEW.md` (if present in current branch) - not present in this branch.
- [x] `.github/ISSUE_TEMPLATE/phase3-inventory-validation-checklist.md`

## Validation commands

Run from repo root unless noted:

```powershell
# 1) Build (required)
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001

# 2) Optional release compile sanity
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Release /p:Platform=x64 /p:CodePage=65001

# 3) Phase 3 inventory validation contract tests
& "blazeclaw/bin/Debug/BlazeClawMfc.Tests.exe" "[skills][inventory][phase3]" --reporter console --success
```

## Completion criteria

- [x] All five required report categories are implemented.
- [x] Report generation is non-destructive and reproducible.
- [x] Tests and docs are updated and cross-linked.
- [x] Build validation completed.
- [x] Rollout tracking row updated to `Done` with `Completed Date`.

## Completion evidence

- Runtime implementation:
  - `blazeclaw/BlazeClawMfc/src/core/SkillsInventoryValidationService.h`
  - `blazeclaw/BlazeClawMfc/src/core/SkillsInventoryValidationService.cpp`
- Tests:
  - `blazeclaw/BlazeClawMfc/tests/SkillsInventoryValidationServiceTests.cpp`
- Reproducible invocation:
  - `blazeclaw/tools/Invoke-SkillsInventoryValidationPhase3.ps1`
