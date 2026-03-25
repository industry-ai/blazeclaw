---
name: self-evolving-bootstrap-reminder
description: "Injects self-evolving reminder content during BlazeClaw bootstrap lifecycle."
metadata:
  {
    "blazeclaw": {
      "stage": "phase-5-adapter",
      "eventContract": ["agent.bootstrap.normalized"],
      "runtimeWiring": "phase-6"
    }
  }
---

# Self-Evolving Hook (BlazeClaw Adapter)

Provides a BlazeClaw-adapted hook contract for reminder injection during
agent bootstrap.

## Trigger Contract

This hook expects a normalized bootstrap event shape:

- `event.type == "agent"`
- `event.action == "bootstrap"`
- `event.context.bootstrapFiles` is an array

The Phase 6 runtime integration layer is responsible for mapping BlazeClaw
lifecycle signals (session bootstrap/reset) into this normalized event.

## Behavior

- Validates event structure before mutation
- Skips sub-agent sessions via `sessionKey` guard
- Injects one virtual bootstrap file:
  - `SELF_EVOLVING_REMINDER.md`

## Runtime Status

- Phase 5: handler contract and metadata implemented in skill package
- Phase 6: C++ gateway/runtime wiring will invoke this contract
