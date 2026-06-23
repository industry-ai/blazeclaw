---
name: "Phase 7 - Future Stability Checklist"
about: "Rollout tracker for Phase 7 future stability guardrail validation"
title: "[Phase7] Future Stability Guardrails"
labels: ["skills", "portability", "phase7"]
assignees: []
---

## Phase Metadata

- Phase: `Phase 7 - Keep BlazeClaw skills future-stable`
- Source of truth: `docs/skill/blazeclaw-openclaw-skill-porting-plan.md` (Phase 7)
- Rollout plan: `docs/skill/phase7-future-stability-rollout-plan.md`
- Status: `Done`
- Completion date: `2026-06-23`

---

## Required Phase 7 Checks

- [x] Workflow-specific assumptions are not encoded in skill files.
- [x] BlazeClaw-specific behavior is represented via runtime adapters/config.
- [x] Protected families (`self-evolving`, `imap-smtp-email`, search, `web-browsing`) remain in stable directories.
- [x] Skill directory renames include compatibility alias or managed migration metadata.
- [x] OpenClaw-compatible `SKILL.md` frontmatter contract remains preserved.

---

## Implementation Artifacts

- [x] Runtime future-stability validator service added:
  - `BlazeClawMfc/src/core/SkillsFutureStabilityValidationService.h`
  - `BlazeClawMfc/src/core/SkillsFutureStabilityValidationService.cpp`
- [x] Tests added:
  - `BlazeClawMfc/tests/SkillsFutureStabilityValidationServiceTests.cpp`
- [x] Invocation script added:
  - `tools/Invoke-SkillsFutureStabilityPhase7.ps1`
- [x] Project wiring updated:
  - `BlazeClawMfc.Tests/BlazeClawMfc.Tests.vcxproj`
  - `BlazeClawMfc/BlazeClawMfc.vcxproj`

---

## Validation Commands

- [x] Required build command:

```powershell
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
```

- [x] Phase 7 guardrail tests:

```powershell
blazeclaw/bin/Debug/BlazeClawMfc.Tests.exe "[skills][future-stability][phase7]" --reporter console --success
```

---

## Associated Docs Synced

- [x] `docs/skill/blazeclaw-openclaw-skill-porting-plan.md`
- [x] `docs/skill/blazeclaw-openclaw-skill-workflow-analysis.md`
- [x] `docs/skill/skill.md`
- [x] `docs/skill/phase7-future-stability-rollout-plan.md`
- [x] `docs/reviews/WORKSTREAM_A_STEP7_SPEECH_BRIDGE_COORDINATOR.md`
- [x] `docs/PROJECT_REVIEW.md` updated only if present in branch (not present in current branch)

---

## Completion Evidence

- [x] Build/test evidence captured in this change set.
- [x] Phase 7 tracker set to `Done`.
- [x] Validation is report-driven and non-destructive.
