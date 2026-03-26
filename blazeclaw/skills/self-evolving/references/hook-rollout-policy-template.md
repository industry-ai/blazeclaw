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
- [ ] Organization policy engine profiles validated
- [ ] Attestation pipeline dry-run succeeded
- [ ] Scorecard publication pipeline dry-run succeeded
- [ ] Tenant policy registry binding validated for rollout scope
- [ ] Centralized attestation authority trust chain validated
- [ ] Outage simulation drills executed for registry and authority
- [ ] Automated failover runbooks validated in target environment
- [ ] Outage outcome promoter scripts executed and outputs reviewed
- [ ] Tenant trend history updated and recommendation score bands reviewed
- [ ] Policy profile weight file reviewed for rollout scope

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
  - policy-as-code controls pass for canary tenant scope
  - primary policy engine profile returns expected decisions
  - tenant registry namespace and bundle resolution verified
  - registry outage simulation pass recorded for canary scope

### Phase R2 — Limited

- Audience: `<x% tenants/workspaces>`
- Duration: `<hours/days>`
- Additional criteria:
  - no sustained `reminder_skipped` anomalies
  - no guard rejection spikes
  - federated scorecard generated for participating tenants
  - attestation pipeline publishes verifiable evidence manifests
  - centralized authority verification passes for limited scope
  - authority outage simulation and failover drill pass recorded

### Phase R3 — Broad

- Audience: `<y% tenants/workspaces>`
- Duration: `<hours/days>`
- Criteria:
  - telemetry stable across target packages
  - incident rate within baseline
  - remediation coverage meets federated scorecard threshold
  - scorecard publication pipeline meets publication SLA
  - registry synchronization remains stable across tenant groups
  - automated failover runbooks execute successfully in broad scope

### Phase R4 — General Availability

- Audience: `100% target scope`
- Exit criteria:
  - all success criteria met for `<duration>`
  - sign-off from engineering + operations
  - governance scorecard accepted by security and compliance owners
  - publication verification confirms final scorecards are discoverable
  - centralized authority production sign-off completed
  - production failback validation completed with approval evidence

### Federated Governance Scorecard Gates

| Phase | Required Scorecard Evidence | Gate Owner |
|---|---|---|
| R1 | Tenant-level controls, registry mapping, and registry outage drill snapshot | `<engineering owner>` |
| R2 | Remediation posture, attestation evidence, authority validation, authority drill results, and promoted tuning recommendations | `<operations owner>` |
| R3 | Cross-tenant compliance trend with failover execution, publication status, recommendation adoption status, and score-severity review | `<security owner>` |
| R4 | Final federated scorecard with dependency-segmented trend review, failback approval, publication verification, and critical-score closure evidence | `<engineering + security + operations>` |

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
- `hooks.remediationSloStatus`
- `hooks.lastComplianceAttestationPath`
- `hooks.crossTenantAttestationAggregationStatus`
- `hooks.crossTenantAttestationAggregationCount`
- `hooks.lastCrossTenantAttestationAggregationPath`

Dashboard links:

- `<dashboard-link-1>`
- `<dashboard-link-2>`

Automation endpoints:

- `gateway.runtime.governance.reportStatus`
- `gateway.runtime.governance.remediationPlan`
- `gateway.runtime.governance.executeRemediation`
- `gateway.runtime.governance.attestationStatus`
- `gateway.runtime.governance.aggregationStatus`

Approval gate checklist:

- [ ] `autoRemediationEnabled` matches rollout phase intent
- [ ] `autoRemediationRequiresApproval` enabled for protected environments
- [ ] Approval token management process validated
- [ ] tenant playbook path configured and writable
- [ ] token max-age policy reviewed with security owner
- [ ] telemetry and audit output directories configured and writable
- [ ] scorecard template completed for current rollout phase
- [ ] scorecard exceptions accepted or remediated before phase promotion
- [ ] policy engine profile fallback behavior tested
- [ ] attestation signature verification passing in target environment
- [ ] published scorecard link validation passed for all targets
- [ ] tenant policy registry sync health checks passing
- [ ] centralized authority revocation checks passing
- [ ] outage simulation report attached for current phase
- [ ] automated failover and failback audit logs attached
- [ ] recommendation score distribution reviewed for in-phase tenants
- [ ] high/critical score recommendations have closure or approved hold
- [ ] active policy profiles mapped to approved scoring weight definitions

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

## 8) Related Templates

- `references/hook-governance-policy-template.md`
- `references/federated-remediation-governance-scorecard-template.md`
- `references/enterprise-policy-attestation-publication-template.md`
