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
  - reminder flow is activated through BlazeClaw event-hook lifecycle execution
  - no direct OpenClaw runtime hook API dependency

## Runtime status in BlazeClaw

- Discovery/loading includes nested `blazeclaw/skills` roots.
- Reminder injection is active when:
  - `hooks.engine.enabled=true`
  - `hooks.engine.reminderEnabled=true`
  - self-evolving hook artifacts are present and eligible
- Diagnostics signal:
  - `hooks.engineMode`
  - `hooks.selfEvolvingHookTriggered`
  - `hooks.reminderState`
  - `hooks.reminderReason`

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

- Runtime execution depends on availability of configured TypeScript-capable runner (`bun`, `tsx`, `deno`, or `node` + `ts-node/esm`).

## Follow-Up Enhancements

- Add resilience patterns for registry/authority outages and runtime
  trust-chain recovery workflows.

Policy templates:

- `references/hook-governance-policy-template.md`
- `references/hook-rollout-policy-template.md`
- `references/federated-remediation-governance-scorecard-template.md`
- `references/enterprise-policy-attestation-publication-template.md`
