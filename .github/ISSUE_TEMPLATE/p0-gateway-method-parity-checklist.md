---
name: "P0 Gateway Method Parity Checklist"
about: "Track implementation of P0 OpenClaw -> BlazeClaw gateway method parity families."
title: "[P0][Gateway Parity] <family>: <scope>"
labels: ["blazeclaw", "gateway", "parity", "p0"]
assignees: []
---

## Goal

Implement and validate one or more **P0** method families from
`blazeclaw/docs/GATEWAY_SERVER_METHODS_MECHANICAL_AUDIT.md`.

## Method family checklist (P0)

- [ ] **Execution approvals** (`exec.approvals.*`, `exec.approval.*`)
  - Target files:
    - `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
    - `blazeclaw/BlazeClawMfc/src/gateway/GatewayProtocolSchemaValidator.Request.cpp`
    - `blazeclaw/BlazeClawMfc/src/gateway/GatewayProtocolSchemaValidator.Response.cpp`
- [ ] **Plugin approvals** (`plugin.approval.*`)
  - Target files:
    - `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
    - `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.Surface.cpp`
    - `blazeclaw/BlazeClawMfc/src/gateway/GatewayProtocolContract.cpp`
- [ ] **Node pairing + invoke lifecycle** (`node.*`)
  - Target files:
    - `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
    - `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.Surface.cpp`
    - `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Transport.cpp`
- [ ] **Device pairing/token lifecycle** (`device.*`)
  - Target files:
    - `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
    - `blazeclaw/BlazeClawMfc/src/core/GatewayHostBindingCoordinator.cpp`
    - `blazeclaw/BlazeClawMfc/src/core/ServiceManager.cpp`

## Required implementation checks

- [ ] Method registration added (or alias mapped) for each selected family.
- [ ] Request schema validation added/updated.
- [ ] Response schema validation added/updated.
- [ ] Error codes and payload shapes aligned with OpenClaw contract intent.
- [ ] Catch2 tests added/updated for each added method family.
- [ ] Docs updated:
  - [ ] `blazeclaw/docs/GATEWAY_SERVER_METHODS_MECHANICAL_AUDIT.md`
  - [ ] `blazeclaw/docs/index.md`
  - [ ] `blazeclaw/docs/architecture.md` (if flow/boundary changed)

## Required tests

- [ ] Gateway protocol contract tests (method-specific).
- [ ] Gateway parity tests for touched method families.
- [ ] Any existing regression suites affected by policy/security changes.

## Validation commands

Run from repo root unless noted:

```powershell
# 1) Build (Debug)
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001

# 2) Run validation harness with tests (CWD blazeclaw/)
powershell -ExecutionPolicy Bypass -File "blazeclaw/BlazeClawMfc/tools/Invoke-BlazeClawOptimizationValidation.ps1" -RunTests -Configuration Debug

# 3) Optional: Release compile sanity
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Release /p:Platform=x64 /p:CodePage=65001
```

## Scope notes

- OpenClaw source of truth for this backlog:
  - `openclaw/src/gateway/server-methods/`
  - `openclaw/src/gateway/server-methods-list.ts`
  - `openclaw/src/gateway/protocol/`
- BlazeClaw may intentionally diverge; if so, document rationale in
  `blazeclaw/docs/GATEWAY_SERVER_METHODS_MECHANICAL_AUDIT.md`.
