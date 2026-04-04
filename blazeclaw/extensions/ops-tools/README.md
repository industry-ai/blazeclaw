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

### Delivery backend fallback behavior

- `email.schedule` now supports backend-chain fallback in approve flow.
- Default backend order:
  1. `himalaya`
  2. `imap-smtp-email` (Node SMTP CLI)
- Override order with:
  - `BLAZECLAW_EMAIL_DELIVERY_BACKENDS`
  - values separated by comma/semicolon (example: `himalaya,imap-smtp-email`).
- Existing mode control remains:
  - `BLAZECLAW_EMAIL_DELIVERY_MODE=mock_success|mock_failure|auto`

If a backend fails, the runtime attempts the next configured backend.
The final response summary reports the selected backend in `engine`.

This keeps outbound scheduling side effects gated by explicit approval.
