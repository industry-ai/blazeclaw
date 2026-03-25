---
name: self-evolving
description: "Captures learnings, errors, and feature requests for continuous improvement in BlazeClaw sessions."
---

# Self-Evolving Skill

Log learnings and errors to markdown files for continuous improvement. Important patterns can be promoted into workspace guidance files.

## Quick Reference

| Situation | Action |
|-----------|--------|
| Command/operation fails | Log to `.learnings/ERRORS.md` |
| User corrects you | Log to `.learnings/LEARNINGS.md` with category `correction` |
| User wants missing feature | Log to `.learnings/FEATURE_REQUESTS.md` |
| Knowledge was outdated | Log to `.learnings/LEARNINGS.md` with category `knowledge_gap` |
| Found better approach | Log to `.learnings/LEARNINGS.md` with category `best_practice` |

## Skill Layout

- `.learnings/` — runtime logs and entries
- `assets/` — reusable templates
- `references/` — setup and integration guidance
- `scripts/` — helper scripts
- `hooks/blazeclaw/` — dedicated event-hook adapter artifacts

## Logging Targets

- `blazeclaw/skills/self-evolving/.learnings/LEARNINGS.md`
- `blazeclaw/skills/self-evolving/.learnings/ERRORS.md`
- `blazeclaw/skills/self-evolving/.learnings/FEATURE_REQUESTS.md`

## Promotion Targets

When a pattern is proven, promote it to:

- `AGENTS.md` (workflow/process)
- `SOUL.md` (behavior)
- `TOOLS.md` (tool gotchas)
- `MEMORY.md` (durable context)
- `.github/copilot-instructions.md` (editor assistant guidance)

## References

- `references/examples.md`
- `references/hooks-setup.md`
- `references/openclaw-integration.md` (BlazeClaw content in legacy filename)

## Usage Guide (BlazeClaw)

1. Ensure the skill exists at `blazeclaw/skills/self-evolving/`.
2. Ensure hook adapter artifacts exist:
   - `hooks/blazeclaw/HOOK.md`
   - `hooks/blazeclaw/handler.ts`
3. Start BlazeClaw runtime from repo root.
4. Verify prompt lifecycle injection via diagnostics report field:
   - `hooks.selfEvolvingHookTriggered: true`
   - `skills.selfEvolvingReminderInjected: true` (fallback/legacy visibility)

## Rollout Checklist

- Verify `.learnings/*` files are writable.
- Verify reminder appears in prompt lifecycle when skill is eligible.
- Verify scripts run in your shell environment:
  - `scripts/activator.sh`
  - `scripts/error-detector.sh`
  - `scripts/extract-skill.sh --dry-run`
- Add team-facing guidance entry in `.github/copilot-instructions.md`.

## Known Limitations

- Handler execution currently uses a constrained adapter that derives bootstrap mutation paths from handler contract content, not a full TypeScript runtime.
- Shell helpers are Bash-oriented; PowerShell equivalents are a follow-up enhancement.

## Follow-Up Enhancements

- Add `.ps1` script variants for Windows-native workflows.
- Add explicit gateway event telemetry around reminder injection transitions.
- Add optional policy toggles to control reminder verbosity.
