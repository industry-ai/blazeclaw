# Ops Tools Extension (BlazeClaw)

Provides concrete runtime tools for prompt-driven operational tasks:

- `weather.lookup`
- `email.schedule` (approval-gated before outbound scheduling)

## Email schedule approval flow
1. Call `email.schedule` with `action=prepare` and message fields.
2. Runtime returns `needs_approval` with `approvalToken`.
3. Call `email.schedule` with `action=approve`, token, and `approve=true|false`.

This keeps outbound scheduling side effects gated by explicit approval.
