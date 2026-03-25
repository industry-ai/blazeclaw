# Hook Governance Policy Template (Tenant-Level)

Use this template to define governance controls for multi-package hook ecosystems in BlazeClaw.

## 1) Policy Metadata

- Tenant ID: `<tenant-id>`
- Policy Version: `<version>`
- Owner Team: `<team>`
- Effective Date: `<yyyy-mm-dd>`
- Review Cadence: `<weekly|monthly|quarterly>`

## 2) Scope

- Included hook packages:
  - `<package-1>`
  - `<package-2>`
- Excluded hook packages:
  - `<package-a>`

- Allowed events:
  - `agent.bootstrap`
  - `<other-supported-events>`

## 3) Trust and Approval Model

- Source control trust requirements:
  - Branch protection required: `<yes|no>`
  - Signed commits required: `<yes|no>`
  - CODEOWNERS review required: `<yes|no>`

- Hook approval workflow:
  - Risk class A (high): `<approval-rules>`
  - Risk class B (medium): `<approval-rules>`
  - Risk class C (low): `<approval-rules>`

## 4) Runtime Guardrails

- Runtime mode policy:
  - Preferred: `<bun|tsx|node-ts-node|deno>`
  - Allowed fallback list: `<ordered-list>`

- Mutation policy:
  - Allowed operations: `bootstrapFiles.append`
  - Disallowed operations: `<list>`
  - Path constraints:
    - Relative paths only: `true`
    - Traversal disallowed: `true`
    - Absolute path disallowed: `true`

- Timeout policy:
  - Per-hook timeout ms: `<value>`
  - Max hooks per dispatch: `<value>`

## 5) Rollout Safety Controls

- Hook engine enablement:
  - `hooks.engine.enabled`: `<true|false>`
- Prompt fallback policy:
  - `hooks.engine.fallbackPromptInjection`: `<true|false>`
- Reminder policy:
  - `hooks.engine.reminderEnabled`: `<true|false>`
  - `hooks.engine.reminderVerbosity`: `<minimal|normal|detailed>`
- Package governance policy:
  - `hooks.engine.allowPackage`: `<repeatable-package-entry>`
  - `hooks.engine.strictPolicyEnforcement`: `<true|false>`

## 6) Observability Requirements

Required diagnostics fields to monitor:

- `hooks.engineMode`
- `hooks.hookDispatchCount`
- `hooks.hookFailureCount`
- `hooks.reminderTriggered`
- `hooks.reminderInjected`
- `hooks.reminderSkipped`
- `hooks.reminderState`
- `hooks.reminderReason`
- `hooks.policyBlocked`
- `hooks.driftDetected`
- `hooks.lastDriftReason`
- `hooks.governanceReportsGenerated`
- `hooks.lastGovernanceReportPath`

Alerting thresholds:

- Failure ratio threshold: `<value>`
- Timeout threshold: `<value>`
- Guard rejection threshold: `<value>`

## 7) Incident Response

- Immediate rollback control:
  - Set `hooks.engine.enabled=false`
- Fallback control:
  - Set `hooks.engine.fallbackPromptInjection=true`

Runbook links:

- Incident runbook: `<link>`
- Owner escalation: `<link|contact>`
- Postmortem template: `<link>`

Governance reporting artifacts:

- Reporting enabled: `hooks.engine.governanceReportingEnabled=true`
- Report output directory: `hooks.engine.governanceReportDir`
- Expected artifact: `hooks-governance-<timestamp>.json`
- Remediation telemetry enabled: `hooks.engine.remediationTelemetryEnabled=true`
- Remediation telemetry directory: `hooks.engine.remediationTelemetryDir`
- Remediation audit enabled: `hooks.engine.remediationAuditEnabled=true`
- Remediation audit directory: `hooks.engine.remediationAuditDir`
- Compliance attestation enabled: `hooks.engine.complianceAttestationEnabled=true`
- Compliance attestation directory: `hooks.engine.complianceAttestationDir`
- Remediation SLO max drift detected: `hooks.engine.remediationSloMaxDriftDetected`
- Remediation SLO max policy blocked: `hooks.engine.remediationSloMaxPolicyBlocked`

Closed-loop remediation gates:

- Auto-remediation enabled: `hooks.engine.autoRemediationEnabled`
- Approval gate required: `hooks.engine.autoRemediationRequiresApproval`
- Approval token reference: `hooks.engine.autoRemediationApprovalToken`
- Tenant id: `hooks.engine.autoRemediationTenantId`
- Playbook output directory: `hooks.engine.autoRemediationPlaybookDir`
- Token max age (minutes): `hooks.engine.autoRemediationTokenMaxAgeMinutes`

Centralized integration endpoints:

- `gateway.runtime.governance.reportStatus`
- `gateway.runtime.governance.remediationPlan`
- `gateway.runtime.governance.executeRemediation`
- `gateway.runtime.governance.attestationStatus`
- `gateway.runtime.governance.aggregationStatus`

Enterprise SLA governance and aggregation:

- SLA governance enabled: `hooks.engine.enterpriseSlaGovernanceEnabled`
- SLA policy id: `hooks.engine.enterpriseSlaPolicyId`
- Cross-tenant aggregation enabled: `hooks.engine.crossTenantAttestationAggregationEnabled`
- Cross-tenant aggregation directory: `hooks.engine.crossTenantAttestationAggregationDir`

## 8) Compliance and Audit

- Audit artifacts retained:
  - Config snapshots
  - Diagnostics samples
  - Change approvals
- Retention duration: `<days>`
- Audit owner: `<owner>`

## 9) Sign-off

- Engineering Owner: `<name/date>`
- Security Owner: `<name/date>`
- Operations Owner: `<name/date>`
