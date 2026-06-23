---
name: "Phase 5 - Command Invocation Parity Checklist"
about: "Rollout tracker for Phase 5 command invocation parity validation"
title: "[Phase5] Command Invocation Parity Validation"
labels: ["skills", "portability", "phase5"]
assignees: []
---

## Phase Metadata

- Phase: `Phase 5 - Validate command invocation parity`
- Source of truth: `docs/skill/blazeclaw-openclaw-skill-porting-plan.md` (Phase 5)
- Rollout plan: `docs/skill/phase5-command-invocation-parity-rollout-plan.md`
- Status: `Done`
- Completion date: `2026-06-23`

---

## Required Phase 5 Checks

- [x] Sanitized command name parity validated.
- [x] Reserved-name dedupe behavior validated.
- [x] `/skill <name>` resolution parity validated.
- [x] Direct `/<command>` resolution parity validated.
- [x] `command-dispatch` behavior parity validated.
- [x] Prompt-template rewrite behavior parity validated.

---

## Implementation Artifacts

- [x] Runtime parity validator service added:
  - `BlazeClawMfc/src/core/SkillsCommandInvocationParityValidationService.h`
  - `BlazeClawMfc/src/core/SkillsCommandInvocationParityValidationService.cpp`
- [x] Tests added:
  - `BlazeClawMfc/tests/SkillsCommandInvocationParityValidationServiceTests.cpp`
- [x] Invocation script added:
  - `tools/Invoke-SkillsCommandInvocationParityPhase5.ps1`
- [x] Project wiring updated:
  - `BlazeClawMfc.Tests/BlazeClawMfc.Tests.vcxproj`

---

## Validation Commands

- [x] Required build command:

```powershell
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001
```

- [x] Phase 5 parity tests:

```powershell
blazeclaw/bin/Debug/BlazeClawMfc.Tests.exe "[skills][invocation][parity][phase5]" --reporter console --success
```

---

## Associated Docs Synced

- [x] `docs/skill/blazeclaw-openclaw-skill-porting-plan.md`
- [x] `docs/skill/blazeclaw-openclaw-skill-workflow-analysis.md`
- [x] `docs/skill/skill.md`
- [x] `docs/skill/phase5-command-invocation-parity-rollout-plan.md`
- [x] `docs/reviews/WORKSTREAM_A_STEP7_SPEECH_BRIDGE_COORDINATOR.md`
- [x] `docs/PROJECT_REVIEW.md` updated only if present in branch (not present in current branch)

---

## Completion Evidence

- [x] Build/test evidence captured in this change set.
- [x] Phase 5 tracker set to `Done`.
- [x] Validation is report-driven and non-destructive.
