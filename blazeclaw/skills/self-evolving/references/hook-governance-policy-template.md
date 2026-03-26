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

## 6.1) Policy-as-Code Rollout Controls

Define explicit control mapping so rollout gates can be evaluated by
automation and human reviewers consistently.

| Control ID | Policy Statement | Enforcement Stage | Rule Source | Owner | Exception Process |
|---|---|---|---|---|---|
| `POL-HOOK-001` | Hook packages must be allowlisted | pre-merge + runtime | `<repo/policy/path>` | `<team>` | `<ticket workflow>` |
| `POL-HOOK-002` | High-risk remediation requires approval token | runtime | `<repo/policy/path>` | `<team>` | `<ticket workflow>` |
| `POL-HOOK-003` | Drift and policy block SLOs must stay below thresholds | post-deploy | `<repo/policy/path>` | `<team>` | `<ticket workflow>` |

Policy bundle workflow:

- Bundle identifier: `<policy-bundle-id>`
- Bundle version pinning: `<semver|git-sha>`
- Validation command: `<policy test command>`
- CI gate required: `<true|false>`
- Deployment gate required: `<true|false>`

## 6.2) Organization-Specific Policy Engine Profiles

Use one or more policy engines to enforce the same control set across
different organization environments.

| Engine Profile | Engine Type | Policy Source | Validation Command | Decision Mode | Owner |
|---|---|---|---|---|---|
| `org-prod-opa` | `<opa|conftest|sentinel|custom>` | `<repo/path-or-registry-ref>` | `<command>` | `<enforce|audit>` | `<team>` |
| `org-regional-eu` | `<opa|conftest|sentinel|custom>` | `<repo/path-or-registry-ref>` | `<command>` | `<enforce|audit>` | `<team>` |

Engine precedence and conflict policy:

- Evaluation order: `<ordered-profile-list>`
- Tie-breaker policy: `<deny-wins|majority|priority-engine>`
- Fallback behavior when engine unavailable: `<fail-closed|fail-open>`

Tenant policy registry wiring:

- Tenant policy registry endpoint: `<registry-url>`
- Tenant registry namespace: `<org/env/tenant>`
- Bundle resolution policy: `<latest-approved|pinned-version>`
- Registry authentication mode: `<managed-identity|oidc|token>`
- Registry synchronization cadence: `<duration>`
- Registry outage fallback bundle: `<bundle-id@version>`
- Registry outage simulation required: `<true|false>`
- Registry failover runbook reference: `<runbook-link-or-id>`

## 6.3) Attestation Pipeline Controls

- Attestation pipeline enabled: `<true|false>`
- Attestation pipeline id: `<pipeline-id>`
- Evidence manifest path: `<path>`
- Required attestations:
  - policy bundle digest attestation
  - runtime config snapshot attestation
  - remediation execution attestation
- Signing authority: `<kms-key|sigstore-identity|internal-ca>`
- Verification command: `<attestation verify command>`
- Promotion gate on verification failure: `<hold|rollback>`

Centralized attestation authority wiring:

- Authority endpoint: `<authority-url>`
- Trust anchor id: `<trust-anchor-id>`
- Certificate/JWKS source: `<secret-ref-or-uri>`
- Issuance policy id: `<policy-id>`
- Revocation check mode: `<strict|best-effort>`
- Failover authority endpoint: `<optional-authority-url>`
- Authority outage simulation required: `<true|false>`
- Authority failover runbook reference: `<runbook-link-or-id>`

## 6.4) Automated Scorecard Publication Controls

- Publication pipeline enabled: `<true|false>`
- Publication trigger: `<phase-complete|scheduled|manual>`
- Publication target(s): `<dashboard|artifact-store|gitops-repo>`
- Publication format: `<json|markdown|both>`
- Publication SLA: `<duration>`
- Publication approval required: `<true|false>`
- Publication evidence retention days: `<days>`

## 7) Incident Response

- Immediate rollback control:
  - Set `hooks.engine.enabled=false`
- Fallback control:
  - Set `hooks.engine.fallbackPromptInjection=true`

Runbook links:

- Incident runbook: `<link>`
- Owner escalation: `<link|contact>`
- Postmortem template: `<link>`
- Registry failover runbook: `<link|id>`
- Authority failover runbook: `<link|id>`

Simulation and failover governance:

- Outage drill cadence: `<weekly|biweekly|monthly>`
- Last drill evidence path: `<path>`
- Auto-failover mode: `<enabled|disabled>`
- Failback approval required: `<true|false>`

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

Federated scorecard controls:

- Scorecard generation enabled: `<true|false>`
- Scorecard storage path: `<path>`
- Scorecard publication target: `<dashboard|artifact-store>`
- Scorecard publication pipeline id: `<pipeline-id>`
- Scorecard publication verification required: `<true|false>`
- Registry mapping verification required: `<true|false>`
- Authority trust-chain verification required: `<true|false>`
- Outage simulation evidence required: `<true|false>`
- Automated failover evidence required: `<true|false>`
- Cross-tenant remediation coverage threshold: `<percent>`
- Cross-tenant policy compliance threshold: `<percent>`
- Scorecard review cadence: `<weekly|monthly|quarterly>`

Outage outcome recommendation controls:

- Outage outcome promoter automation enabled: `<true|false>`
- Learning promotion target path:
  `blazeclaw/skills/self-evolving/.learnings/LEARNINGS.md`
- Policy tuning recommendation path:
  `blazeclaw/skills/self-evolving/.learnings/POLICY_TUNING_RECOMMENDATIONS.md`
- Trend history path:
  `blazeclaw/skills/self-evolving/.learnings/OUTAGE_TREND_HISTORY.csv`
- Profile scoring weights path:
  `blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.csv`
- Signed profile manifest path:
  `blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest`
- Default trend window size: `<n>`
- Trend segmentation mode: `tenant + dependency class`
- Required promoter inputs: `tenant-id`, `rollout-phase`, `policy-profile`
- Optional strict schema pin: `strict-schema-version`
- Optional strict signed-manifest pin: `require-signed-manifest`
- Optional cryptographic verification mode pin:
  `signature-verification-mode (none|kms|sigstore)`
- KMS verifier dependency requirement: `openssl`
- Sigstore verifier dependency requirement: `cosign`
- Recommendation review SLA: `<duration>`
- Recommendation promotion approvers: `<engineering|security|operations>`
- Fail-fast validation policy:
  - missing profile weights file: `deny rollout gate`
  - missing profile row: `deny rollout gate`
  - malformed numeric weights/divisors: `deny rollout gate`
  - strict schema mismatch (when enabled): `deny rollout gate`
  - signed manifest missing/malformed/digest mismatch (when enabled): `deny rollout gate`
  - signature mode mismatch or cryptographic verification failure (when
    enabled): `deny rollout gate`
- Recommendation severity gating:
  - `critical|high` in `r3|r4` requires explicit hold or remediation approval
  - `medium` requires owner review before phase promotion
  - `low` can be auto-queued for routine follow-up

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

## 10) Related Templates

- `references/hook-rollout-policy-template.md`
- `references/federated-remediation-governance-scorecard-template.md`
- `references/enterprise-policy-attestation-publication-template.md`
