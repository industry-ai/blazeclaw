# Lobster Workflow Extension (BlazeClaw)

This extension provides a deterministic workflow tool surface for parity with OpenClaw workflow-style execution.

## Tool
- `lobster`
  - `action=run`: execute deterministic pipeline envelope
  - `action=resume`: continue approval-gated run using token

## Notes
- Current implementation is gateway-native runtime execution with deterministic JSON envelopes.
- Full OpenClaw parity (subprocess/runtime plugin lifecycle and full pipeline command support) is tracked separately.
