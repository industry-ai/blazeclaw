# Notion 2-Minute Smoke Check

## Goal
Daily sanity check for Notion config + runtime path with minimal steps.

## Steps (target: <= 2 minutes)

1) **Open Notion config**
- Skill Browser -> click `notion`.
- Expect route evidence:
  - `skills.config.route ... mode=dedicated`

2) **Confirm credential field exists**
- Expect `NOTION_API_KEY` input is visible.
- If no field: fail smoke immediately.

3) **Save quick config pass**
- Paste/update `NOTION_API_KEY`.
- Click `Save`.
- Expect:
  - `blazeclaw.skill.config.saved`
  - returned `configPath`

4) **Reload check**
- Reopen Notion config tab.
- Expect:
  - `blazeclaw.skill.config.loaded`
  - persisted value behavior present.

5) **One live Notion action**
- Run one Notion operation from chat/tool flow.
- Expect no missing-credential/auth-missing error.

## Pass/Fail
- **PASS**: all 5 steps succeed.
- **FAIL**: any missing field/save/load/auth issue.

## Fast Failure Triage
- No fields: check route mode and metadata payload contract.
- Save error: inspect `blazeclaw.skill.config.error`.
- Auth error after save: verify key validity + integration access to target page/database.

## Links
- Full live script:
  - `skills-bundled/notion/PHASE5_LIVE_MANUAL_CHECK_SCRIPT.md`
- Troubleshooting:
  - `skills-bundled/notion/TROUBLESHOOTING.md`
