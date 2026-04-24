# Notion Phase 5 Live Manual Check Script

## Purpose
Fast, repeatable runtime verification for Notion skill configuration and execution in BlazeClaw.

## Preconditions
- BlazeClaw app builds and launches.
- Network can reach Notion API.
- You have a valid Notion integration key (`ntn_...` or `secret_...`).
- The integration is shared to target Notion page/database.

## Quick Inputs
- `NOTION_API_KEY`: `<your_notion_key>`
- Optional target page/database name: `<target_name>`

## Step-by-Step Script

### Step 1 - Launch BlazeClaw
Action:
- Start BlazeClaw and wait for UI fully ready.

Expected:
- Main UI loads with Skill Browser visible.

---

### Step 2 - Open Notion from Skill Browser
Action:
- In left Skill Browser, click `notion`.

Expected:
- A skill config tab opens.
- Status output includes route evidence:
  - `skills.config.route ... mode=dedicated`
- Config page title shows Notion configuration form.

Failure hint:
- If route shows `mode=generated`, continue with generated fallback checks in Step 3/4 and inspect metadata contract.

---

### Step 3 - Verify Notion credential field is present
Action:
- Inspect config page fields.

Expected:
- `NOTION_API_KEY` input is visible and editable.

Failure hint:
- If no fields render and generated warning appears:
  - "No configuration fields were exposed for this skill..."
  - check `skills-bundled/notion/TROUBLESHOOTING.md`.

---

### Step 4 - Save Notion config
Action:
- Enter `NOTION_API_KEY`.
- Click `Save`.

Expected:
- UI status transitions to saving then success.
- Bridge response contains:
  - `blazeclaw.skill.config.saved`
  - `configPath` present
- Runtime refresh path is triggered (`gateway.skills.refresh`).

Failure hint:
- If save fails, expected channel:
  - `blazeclaw.skill.config.error`

---

### Step 5 - Reload config tab and verify persistence
Action:
- Close Notion config tab.
- Reopen `notion` from Skill Browser.

Expected:
- Previously saved key is loaded by skill-config bridge.
- Bridge load response:
  - `blazeclaw.skill.config.loaded`
- Page shows persisted value behavior (masked/plain per field type implementation).

Failure hint:
- If value is missing, inspect canonical skill-config persistence path reported in `saved` payload.

---

### Step 6 - Runtime metadata sanity check (optional parity command)
Action:
- Trigger parity menu command for skills list/status, or inspect runtime skill payload in diagnostics.

Expected:
- Notion entry includes config metadata:
  - `primaryEnv = NOTION_API_KEY`
  - `requiresEnv` contains `NOTION_API_KEY`

---

### Step 7 - Live Notion execution check
Action:
- Run a Notion-related request in chat/tool flow, e.g. write/update/search operation.

Expected:
- Tool call executes without missing-credential error.
- No unauthorized/auth-missing failure attributable to absent key.

Failure hint:
- If auth fails despite saved key:
  - verify key validity
  - verify integration has access to target Notion page/database
  - verify Notion version/header requirements in `SKILL.md`.

---

## Pass Criteria
- Notion opens with credential input.
- Save succeeds and returns `configPath`.
- Reopen reloads persisted config.
- Live Notion operation runs without missing-key failure.

## Fast Triage Map
- No fields shown -> check route mode, then metadata/payload contract.
- Save error -> inspect `blazeclaw.skill.config.error`.
- Saved but runtime fails -> check integration permission + key validity.

## Related Docs
- `skills-bundled/notion/PORTING_PLAN.md`
- `skills-bundled/notion/PHASE5_RUNTIME_VALIDATION.md`
- `skills-bundled/notion/TROUBLESHOOTING.md`
