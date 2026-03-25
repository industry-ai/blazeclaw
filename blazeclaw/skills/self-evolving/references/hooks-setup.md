# Hook Setup Guide (BlazeClaw)

Configure self-evolving reminders for coding workflows.

## Current Status

Runtime integration provides reminder injection through BlazeClaw
event-hook execution lifecycle.

This is an adapter model for BlazeClaw runtime (not a direct clone of
OpenClaw's `agent:bootstrap` hook runtime).

You can still use these options for local workflow reinforcement:

1. **Manual reminder pattern** in your workflow documents (`AGENTS.md`, `BOOTSTRAP.md`, `.github/copilot-instructions.md`)
2. **Shell-level hook scripts** via your coding tools (for example Claude Code/Codex local hook settings)
3. **Periodic review task** that prompts logging to `.learnings/`

## Recommended Interim Setup

Add this section to `.github/copilot-instructions.md` in your repo:

```markdown
## Self-Evolving Reminder

After tasks that involve corrections, failures, or new recurring patterns:
- Log learnings to `skills/self-evolving/.learnings/LEARNINGS.md`
- Log failures to `skills/self-evolving/.learnings/ERRORS.md`
- Log missing capabilities to `skills/self-evolving/.learnings/FEATURE_REQUESTS.md`
```

## Optional Local Tool Hooks

If your local CLI supports shell hooks, point them to scripts under:

- `blazeclaw/skills/self-evolving/scripts/activator.sh`
- `blazeclaw/skills/self-evolving/scripts/error-detector.sh`

(These scripts are ported in Phase 4.)

## Verification

- Confirm `.learnings/` files exist under `blazeclaw/skills/self-evolving/`
- Confirm your workflow guidance includes explicit logging instructions
- Confirm at least one test entry can be appended without format errors
- Confirm runtime diagnostics include:
  - `hooks.selfEvolvingHookTriggered: true` when self-evolving hook executed
  - `hooks.hookDispatchCount > 0` during active runs
  - `hooks.hookFailureCount == 0` for healthy dispatch

## Notes

This document intentionally avoids OpenClaw-specific commands and events.
Phase 5 established hook metadata and handler contract files under:

- `blazeclaw/skills/self-evolving/hooks/blazeclaw/HOOK.md`
- `blazeclaw/skills/self-evolving/hooks/blazeclaw/handler.ts`

Runtime integration activates reminder injection when:

- the `self-evolving` skill is included in eligible prompt skills, and
- `hooks/blazeclaw/HOOK.md` plus `hooks/blazeclaw/handler.ts` are present.

Prompt-side reminder injection is retained only as optional fallback via
`hooks.engine.fallbackPromptInjection`.
