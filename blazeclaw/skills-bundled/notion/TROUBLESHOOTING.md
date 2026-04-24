# Notion Skill Troubleshooting (BlazeClaw)

## No fields shown in Skill Browser config page
- Expected route for Notion is dedicated page:
  - `blazeclaw/skills-bundled/notion/config.html`
- Check chat/status output for:
  - `skills.config.route ... mode=dedicated`
  - If you see `mode=generated`, dedicated page was not resolved.
- If generated page appears and shows warning:
  - "No configuration fields were exposed for this skill..."
  - verify skill payload metadata contains `primaryEnv` / `requiresEnv`.

## Config saved but Notion still unauthorized
- Confirm save response includes `blazeclaw.skill.config.saved`.
- Reopen config page and verify `NOTION_API_KEY` is loaded.
- Ensure key is valid and integration is shared with target Notion pages/databases.
- After save, runtime refresh is triggered through:
  - `gateway.skills.refresh`

## Quick verification steps
1. Open Skill Browser -> Notion.
2. Enter `NOTION_API_KEY` and click Save.
3. Reopen Notion config and confirm key reload behavior.
4. Run a Notion tool action and verify no credential-missing error.

