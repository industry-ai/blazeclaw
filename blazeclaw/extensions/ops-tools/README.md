# Ops Tools Extension (BlazeClaw)

Provides concrete runtime tools for prompt-driven operational tasks:

- `weather.lookup`
- `email.schedule` (approval-gated before outbound scheduling)

Runtime backend notes:

- `weather.lookup` is served by the in-process runtime provider adapter
  `blazeclaw.weather.provider.v1`.
- `email.schedule` is served by the in-process runtime scheduler adapter
  `blazeclaw.email.scheduler.backend.v1`.
- Email approval tokens are persisted via `ApprovalTokenStore` and validated with TTL
  on `approve` requests.

## Compound prompt orchestration parity (Gateway runtime)

`GatewayHost` now includes deterministic decomposition for weather/report/email prompts.

Supported prompt semantics:

- Weather intent + email intent + recipient email detection.
- Schedule detection via either explicit time (`3pm`, `14:30`) or immediate keywords:
  - `now`
  - `right now`
  - `immediately`
- Date detection for `today` and `tomorrow`.

Decomposition stages (sequential):

1. `weather.lookup`
2. `report.compose` (internal deterministic stage)
3. `email.schedule` with `action=prepare` (approval-gated)

Observability telemetry:

- `gateway.chat.orchestration.intent`
  - includes matched/miss diagnostics and schedule resolution kind.
- `gateway.chat.orchestration.execution`
  - includes success/failure and decomposition step count.

## Email schedule approval flow
1. Call `email.schedule` with `action=prepare` and message fields.
2. Runtime returns `needs_approval` with `approvalToken`.
3. Call `email.schedule` with `action=approve`, token, and `approve=true|false`.

This keeps outbound scheduling side effects gated by explicit approval.
