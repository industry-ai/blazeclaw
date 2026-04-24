# Notion Phase 5 Runtime Validation

## Goal
Validate config persistence and runtime consumption for Notion after Phase 1-4 changes.

## Validation Checklist
- [x] Save path uses generic skill-config bridge (`blazeclaw.skill.config.save`).
- [x] Load path uses generic skill-config bridge (`blazeclaw.skill.config.ready` / `loaded`).
- [x] Persist path remains `CBlazeClawMFCDoc::SaveSkillConfigEnv(...)`.
- [x] Reload path remains `CBlazeClawMFCDoc::LoadSkillConfigEnv(...)`.
- [x] Save triggers runtime refresh (`gateway.skills.refresh`).
- [x] Config route diagnostics added (`skills.config.route`).

## Manual Runtime Verification (Operator Runbook)
1. Open Skill Browser -> `notion`.
2. Confirm route line contains `mode=dedicated`.
3. Enter `NOTION_API_KEY` and click Save.
4. Confirm `blazeclaw.skill.config.saved` appears and path is returned.
5. Close and reopen Notion config page; confirm value reload.
6. Execute a Notion skill operation and verify no credential-missing failure.

## Notes
- Full end-to-end API validation depends on operator-provided valid Notion credentials and reachable Notion API.
- Live execution checklist:
  - `blazeclaw/skills-bundled/notion/PHASE5_LIVE_MANUAL_CHECK_SCRIPT.md`
- Daily quick sanity checklist:
  - `blazeclaw/skills-bundled/notion/PHASE5_SMOKE_2MIN.md`

