# BlazeClaw Integration (legacy filename)

This document replaces OpenClaw-specific integration guidance with BlazeClaw runtime semantics.

## Why this file name remains

The source skill used `openclaw-integration.md`. The filename is kept temporarily for traceability during porting, but content is BlazeClaw-native.

## Workspace and skill locations

For BlazeClaw repository workflows, use:

- Skill path: `blazeclaw/skills/self-evolving/`
- Learning logs:
  - `blazeclaw/skills/self-evolving/.learnings/LEARNINGS.md`
  - `blazeclaw/skills/self-evolving/.learnings/ERRORS.md`
  - `blazeclaw/skills/self-evolving/.learnings/FEATURE_REQUESTS.md`

## Skill metadata compatibility

BlazeClaw skill catalog supports frontmatter with required:

- `name`
- `description`

And supports requirement aliases used by OpenClaw skills, including:

- `openclaw.requires.env` / `requires.env`
- `openclaw.requires.bins` / `requires.bins`
- `openclaw.requires.anybins` / `requires.anybins`
- `openclaw.requires.config` / `requires.config`
- `openclaw.os` / `os`

## Migration Notes: `self-improving-agent` → `self-evolving`

| OpenClaw source | BlazeClaw target |
|---|---|
| `skills/pskoett/self-improving-agent/` | `blazeclaw/skills/self-evolving/` |
| Hook id/name: `self-improvement` | Hook adapter id/name: `self-evolving-bootstrap-reminder` |
| `hooks/openclaw/HOOK.md` | `hooks/blazeclaw/HOOK.md` |
| `hooks/openclaw/handler.ts` | `hooks/blazeclaw/handler.ts` |

### Behavior mapping

- Preserved:
  - bootstrap-only trigger semantics
  - event/context safety guards
  - sub-agent session exclusion
  - virtual reminder content injection intent
- Adapted:
  - reminder flow is activated through BlazeClaw skills prompt lifecycle wiring
  - no direct OpenClaw runtime hook API dependency

## Runtime status in BlazeClaw

- Discovery/loading includes nested `blazeclaw/skills` roots.
- Reminder injection is active when:
  - `self-evolving` is prompt-eligible, and
  - `hooks/blazeclaw/HOOK.md` + `hooks/blazeclaw/handler.ts` are present.
- Diagnostics signal:
  - `skills.selfEvolvingReminderInjected`

## Promotion targets in BlazeClaw workspaces

When a pattern is proven, promote it to persistent guidance:

- `AGENTS.md` for workflow/process patterns
- `SOUL.md` for behavioral patterns
- `TOOLS.md` for tool-specific gotchas
- `MEMORY.md` for durable project context
- `.github/copilot-instructions.md` for editor assistant context

## Validation checklist

- Skill file parses with valid frontmatter
- Learning logs are present and writable
- Reference docs contain no OpenClaw runtime commands
- Follow-up phases implement script + runtime behavior parity

## Known Limitations

- Reminder flow currently uses skills prompt lifecycle dispatch, not a standalone hook runtime.
- Script tooling is Bash-first; PowerShell-native companions are not included yet.

## Follow-Up Enhancements

- Add PowerShell script equivalents for all helper scripts.
- Add finer-grained runtime toggles for reminder injection policy.
- Add dedicated telemetry and trace events for reminder injection path.
