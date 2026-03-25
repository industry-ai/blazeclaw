# Hook Rollout Policy Template (Multi-Package)

Use this template to stage rollout safely across tenants/workspaces for multi-package hook runtime deployment.

## 1) Rollout Identification

- Rollout Name: `<name>`
- Tenant: `<tenant-id>`
- Target Packages:
  - `<package-1>`
  - `<package-2>`
- Rollout Owner: `<team/person>`

## 2) Preconditions Checklist

- [ ] Hook metadata validated (`HOOK.md`)
- [ ] Handler runtime checks passed (`handler.ts`)
- [ ] Fixture validation passed
- [ ] Build validation passed (`msbuild BlazeClaw.sln`)
- [ ] Governance policy approved

## 3) Environment Matrix

| Environment | Engine Enabled | Fallback Prompt Injection | Reminder Enabled | Reminder Verbosity |
|---|---:|---:|---:|---|
| Dev | `<true|false>` | `<true|false>` | `<true|false>` | `<minimal|normal|detailed>` |
| Staging | `<true|false>` | `<true|false>` | `<true|false>` | `<minimal|normal|detailed>` |
| Production | `<true|false>` | `<true|false>` | `<true|false>` | `<minimal|normal|detailed>` |

### Package Governance Matrix

| Environment | Allowed Packages | Strict Policy Enforcement |
|---|---|---:|
| Dev | `<comma-separated-packages>` | `<true|false>` |
| Staging | `<comma-separated-packages>` | `<true|false>` |
| Production | `<comma-separated-packages>` | `<true|false>` |

## 4) Progressive Rollout Phases

### Phase R1 — Canary

- Audience: `<small subset>`
- Duration: `<hours/days>`
- Success criteria:
  - `hookFailureCount` below `<threshold>`
  - `reminderState=reminder_injected` in expected sessions

### Phase R2 — Limited

- Audience: `<x% tenants/workspaces>`
- Duration: `<hours/days>`
- Additional criteria:
  - no sustained `reminder_skipped` anomalies
  - no guard rejection spikes

### Phase R3 — Broad

- Audience: `<y% tenants/workspaces>`
- Duration: `<hours/days>`
- Criteria:
  - telemetry stable across target packages
  - incident rate within baseline

### Phase R4 — General Availability

- Audience: `100% target scope`
- Exit criteria:
  - all success criteria met for `<duration>`
  - sign-off from engineering + operations

## 5) Monitoring Plan

Track at minimum:

- `hooks.engineMode`
- `hooks.hookDispatchCount`
- `hooks.hookFailureCount`
- `hooks.dispatchTimeouts`
- `hooks.guardRejected`
- `hooks.reminderState`
- `hooks.reminderReason`
- `hooks.policyBlocked`
- `hooks.driftDetected`
- `hooks.lastDriftReason`
- `hooks.governanceReportsGenerated`
- `hooks.lastGovernanceReportPath`
- `hooks.lastRemediationTelemetryPath`
- `hooks.lastRemediationAuditPath`

Dashboard links:

- `<dashboard-link-1>`
- `<dashboard-link-2>`

Automation endpoints:

- `gateway.runtime.governance.reportStatus`
- `gateway.runtime.governance.remediationPlan`
- `gateway.runtime.governance.executeRemediation`

Approval gate checklist:

- [ ] `autoRemediationEnabled` matches rollout phase intent
- [ ] `autoRemediationRequiresApproval` enabled for protected environments
- [ ] Approval token management process validated
- [ ] tenant playbook path configured and writable
- [ ] token max-age policy reviewed with security owner
- [ ] telemetry and audit output directories configured and writable

## 6) Rollback Plan

Rollback triggers:

- failure ratio exceeds `<threshold>`
- reminder skip ratio exceeds `<threshold>`
- timeout count exceeds `<threshold>`

Rollback actions:

1. Set `hooks.engine.enabled=false`
2. Set `hooks.engine.fallbackPromptInjection=true`
3. Broadcast incident update to stakeholders
4. Capture diagnostics snapshot for RCA

## 7) Post-Rollout Review

- Review window: `<date-range>`
- Outcomes:
  - success/failure summary
  - package-specific issues
  - policy updates
- Action items:
  - `<item-1>`
  - `<item-2>`
