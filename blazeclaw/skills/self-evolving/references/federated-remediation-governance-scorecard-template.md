# Federated Remediation Governance Scorecard Template

Use this template to track policy-as-code compliance and remediation
posture across tenants during hook runtime rollout.

## 1) Scorecard Metadata

- Scorecard ID: `<scorecard-id>`
- Reporting Window: `<yyyy-mm-dd..yyyy-mm-dd>`
- Generated At: `<timestamp>`
- Scope Type: `<tenant|workspace|federated>`
- Scope Identifier: `<tenant-id|workspace-id|federation-id>`
- Owners:
  - Engineering: `<owner>`
  - Security: `<owner>`
  - Operations: `<owner>`

Policy engine context:

- Engine profiles evaluated: `<profile-a,profile-b>`
- Policy bundle reference: `<bundle-id@version>`
- Decision mode: `<enforce|audit>`

Registry and authority context:

- Tenant policy registry endpoint: `<registry-url>`
- Registry namespace coverage: `<tenant-group-or-scope>`
- Centralized attestation authority endpoint: `<authority-url>`
- Authority trust anchor id: `<trust-anchor-id>`

## 2) Policy-as-Code Control Summary

| Control ID | Description | Pass Count | Fail Count | Exception Count | Status |
|---|---|---:|---:|---:|---|
| `POL-HOOK-001` | Allowlisted package enforcement | `<n>` | `<n>` | `<n>` | `<green|yellow|red>` |
| `POL-HOOK-002` | Approval gate enforcement | `<n>` | `<n>` | `<n>` | `<green|yellow|red>` |
| `POL-HOOK-003` | Drift and policy-block SLO compliance | `<n>` | `<n>` | `<n>` | `<green|yellow|red>` |

## 3) Federated Remediation Posture

| Metric | Current | Target | Trend | Status |
|---|---:|---:|---|---|
| Remediation backlog size | `<n>` | `<n>` | `<up|flat|down>` | `<green|yellow|red>` |
| Policy blocked incidents | `<n>` | `<n>` | `<up|flat|down>` | `<green|yellow|red>` |
| Drift detected incidents | `<n>` | `<n>` | `<up|flat|down>` | `<green|yellow|red>` |
| Auto-remediation success ratio | `<percent>` | `<percent>` | `<up|flat|down>` | `<green|yellow|red>` |
| Approval-gated remediation latency (p95) | `<duration>` | `<duration>` | `<up|flat|down>` | `<green|yellow|red>` |

## 4) Cross-Tenant Aggregation

| Tenant | Compliance Score | Remediation Coverage | Open Exceptions | Score |
|---|---:|---:|---:|---|
| `<tenant-a>` | `<percent>` | `<percent>` | `<n>` | `<green|yellow|red>` |
| `<tenant-b>` | `<percent>` | `<percent>` | `<n>` | `<green|yellow|red>` |

Federation-level aggregates:

- Weighted compliance score: `<percent>`
- Weighted remediation coverage: `<percent>`
- Total open exceptions: `<n>`
- Scorecard status: `<green|yellow|red>`

## 5) Exception and Risk Register

| Exception ID | Tenant | Related Control | Risk Level | Expiry Date | Owner | Mitigation Plan |
|---|---|---|---|---|---|---|
| `<exc-001>` | `<tenant-id>` | `POL-HOOK-001` | `<low|medium|high>` | `<yyyy-mm-dd>` | `<owner>` | `<plan>` |

## 6) Required Artifacts

- Governance report path: `<path>`
- Remediation telemetry path: `<path>`
- Remediation audit path: `<path>`
- Compliance attestation path: `<path>`
- Cross-tenant aggregation path: `<path>`

## 6.1) Attestation Pipeline Evidence

- Pipeline run id: `<pipeline-run-id>`
- Manifest digest: `<sha256>`
- Signature reference: `<signature-uri>`
- Verification result: `<pass|fail>`
- Verification timestamp: `<timestamp>`
- Authority issuer policy id: `<policy-id>`
- Trust-chain validation result: `<pass|fail>`

## 6.2) Scorecard Publication Status

- Publication pipeline id: `<pipeline-id>`
- Publication run id: `<run-id>`
- Publication targets:
  - `<dashboard-url-or-id>`
  - `<artifact-uri>`
- Published formats: `<json|markdown|both>`
- Publication completed at: `<timestamp>`
- Publication verification status: `<pass|fail>`

## 6.3) Registry Binding Evidence

- Tenant mapping snapshot path: `<path>`
- Bundle resolution mode: `<latest-approved|pinned-version>`
- Registry sync status: `<pass|fail>`
- Last sync timestamp: `<timestamp>`

## 6.4) Outage Simulation and Failover Evidence

- Registry simulation id(s): `<sim-id-list>`
- Authority simulation id(s): `<sim-id-list>`
- Drill execution window: `<timestamp-range>`
- Automated failover triggered: `<yes|no>`
- Automated failback completed: `<yes|no>`
- Failover decision log path: `<path>`
- Recovery validation status: `<pass|fail>`

## 6.5) Policy Tuning Recommendation Outcomes

- Recommendation source path:
  `blazeclaw/skills/self-evolving/.learnings/POLICY_TUNING_RECOMMENDATIONS.md`
- Trend history source path:
  `blazeclaw/skills/self-evolving/.learnings/OUTAGE_TREND_HISTORY.csv`
- Profile scoring weights path:
  `blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.csv`
- Signed manifest path:
  `blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest`
- Recommendations generated in window: `<n>`
- Recommendations accepted: `<n>`
- Recommendations deferred: `<n>`
- Recommendations rejected: `<n>`
- Recommendations by severity:
  - critical: `<n>`
  - high: `<n>`
  - medium: `<n>`
  - low: `<n>`
- Tenant trend fail-rate p95: `<percent>`
- Dependency-segmented trend fail-rate p95:
  - registry: `<percent>`
  - authority: `<percent>`
- Active policy profiles evaluated: `<profile-list>`
- Strict manifest gate failures: `<n>`
- Cryptographic verification failures: `<n>`
- Active signature verification modes observed: `<none|kms|sigstore mix>`
- Highest-impact accepted tuning:
  `<short summary with control reference>`

## 7) Review and Sign-off

- Engineering Approval: `<name/date/status>`
- Security Approval: `<name/date/status>`
- Operations Approval: `<name/date/status>`
- Compliance Approval (optional): `<name/date/status>`

## 8) Promotion Decision

- Recommended rollout phase outcome: `<hold|promote|rollback>`
- Decision rationale: `<short rationale>`
- Required follow-up actions:
  - `<action-1>`
  - `<action-2>`
