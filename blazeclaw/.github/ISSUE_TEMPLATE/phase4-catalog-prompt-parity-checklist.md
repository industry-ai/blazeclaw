---
name: "Phase 4 - Catalog & Prompt Parity Checklist"
about: "Rollout tracker for Phase 4 catalog and prompt parity validation"
title: "[Phase4] Catalog & Prompt Parity Validation"
labels: ["skills", "portability", "phase4"]
assignees: []
---

## Phase Metadata

- Phase: `Phase 4 - Validate catalog and prompt parity`
- Source of truth: `docs/skill/blazeclaw-openclaw-skill-porting-plan.md` (Phase 4)
- Rollout plan: `docs/skill/phase4-catalog-prompt-parity-rollout-plan.md`
- Status: `Done`
- Completion date: `2026-06-23`

---

## Required Phase 4 Checks

- [x] Catalog snapshot loads expected OpenClaw-copied skills.
- [x] Catalog `name` parity validated.
- [x] Catalog `description` parity validated.
- [x] Metadata parse result parity validated.
- [x] Eligibility result parity validated.
- [x] Prompt visibility / intentional hiding parity validated.
- [x] Compact fallback retention parity validated.
- [x] Source precedence behavior validated.

---

## Implementation Artifacts

- [x] Runtime parity validator service added:
  - `BlazeClawMfc/src/core/SkillsCatalogPromptParityValidationService.h`
  - `BlazeClawMfc/src/core/SkillsCatalogPromptParityValidationService.cpp`
- [x] Tests added:
  - `BlazeClawMfc/tests/SkillsCatalogPromptParityValidationServiceTests.cpp`
- [x] Invocation script added:
  - `tools/Invoke-SkillsCatalogPromptParityPhase4.ps1`
- [x] Project wiring updated:
  - `BlazeClawMfc.Tests/BlazeClawMfc.Tests.vcxproj`

---

## Validation Commands

- [x] Required build command:

```powershell
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
```

- [x] Phase 4 parity tests:

```powershell
blazeclaw/bin/Debug/BlazeClawMfc.Tests.exe "[skills][catalog][parity][phase4]" --reporter console --success
```

---

## Associated Docs Synced

- [x] `docs/skill/blazeclaw-openclaw-skill-porting-plan.md`
- [x] `docs/skill/blazeclaw-openclaw-skill-workflow-analysis.md`
- [x] `docs/skill/skill.md`
- [x] `docs/skill/phase4-catalog-prompt-parity-rollout-plan.md`
- [x] `docs/reviews/WORKSTREAM_A_STEP7_SPEECH_BRIDGE_COORDINATOR.md`
- [x] `docs/PROJECT_REVIEW.md` updated only if present in branch (not present in current branch)

---

## Completion Evidence

- [x] Build/test evidence captured in this change set.
- [x] Phase 4 tracker set to `Done`.
- [x] Validation is report-driven and non-destructive.
