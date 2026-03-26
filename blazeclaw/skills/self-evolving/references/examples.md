# Entry Examples

Concrete examples of well-formatted entries with all fields.

## Learning: Correction

```markdown
## [LRN-20250115-001] correction

**Logged**: 2025-01-15T10:30:00Z
**Priority**: high
**Status**: pending
**Area**: tests

### Summary
Incorrectly assumed pytest fixtures are scoped to function by default

### Details
When writing test fixtures, I assumed all fixtures were function-scoped.
User corrected that while function scope is the default, the codebase
convention uses module-scoped fixtures for database connections to
improve test performance.

### Suggested Action
When creating fixtures that involve expensive setup (DB, network),
check existing fixtures for scope patterns before defaulting to function scope.

### Metadata
- Source: user_feedback
- Related Files: tests/conftest.py
- Tags: pytest, testing, fixtures

---
```

## Federated Remediation Governance Scorecard Snapshot

```markdown
## [SCORE-20250115-001] federated_governance

**Logged**: 2025-01-15T18:00:00Z
**Window**: 2025-01-08..2025-01-15
**Scope**: federated
**Status**: yellow

### Control Summary
- POL-HOOK-001 allowlist enforcement: pass
- POL-HOOK-002 approval gate enforcement: pass
- POL-HOOK-003 drift/policy SLO: warning

### Federation Metrics
- Weighted compliance score: 96%
- Weighted remediation coverage: 91%
- Open exceptions: 3

### Decision
Hold promotion from R2 to R3 until policy-block incidents are reduced below threshold.

### Artifacts
- Governance report: reports/hooks-governance-20250115.json
- Aggregation report: reports/attestation-aggregate-20250115.json
- Attestation manifest: reports/attestation-manifest-20250115.json
- Published dashboard: https://governance.example/scorecards/20250115

---
```

## Policy Engine + Publication Control Snapshot

```markdown
## [GOV-20250115-002] policy_engine_publication

**Logged**: 2025-01-15T19:15:00Z
**Status**: pass
**Scope**: tenant

### Engine Profile Validation
- Profile: org-prod-opa
- Decision mode: enforce
- Validation command: policyctl validate --profile org-prod-opa
- Result: pass

### Attestation Pipeline
- Pipeline run: att-20250115-77
- Signature verification: pass
- Verification timestamp: 2025-01-15T19:05:00Z
- Central authority endpoint: https://attest.example/authority
- Trust anchor id: trust-anchor-prod-01

### Scorecard Publication
- Pipeline run: pub-20250115-11
- Targets: governance-dashboard, artifact-store
- Publication SLA: met (7m < 10m)

### Tenant Registry Binding
- Registry endpoint: https://policy.example/registry
- Tenant namespace: org/prod/tenant-a
- Bundle resolution: pinned-version (bundle-a@1.3.4)
- Registry sync health: pass

### Outage Simulation and Failover
- Registry simulation: SIM-REG-001 pass
- Authority simulation: SIM-AUTH-001 pass
- Automated failover triggered: yes
- Automated failback completed: yes
- Drill evidence: reports/drills/failover-20250115.json

---
```

## Outage Simulation Outcome Promotion + Policy Tuning

```markdown
## [LRN-OUT-20250115-SIMAUTH001] knowledge_gap

**Logged**: 2025-01-15T20:10:00Z
**Priority**: high
**Status**: pending
**Area**: infra

### Summary
Authority outage simulation SIM-AUTH-001 failed trust-chain validation fallback

### Details
- Simulation ID: SIM-AUTH-001
- Tenant ID: tenant-a
- Rollout Phase: r3
- Policy Profile: org-dr-sentinel
- Policy Profile Schema Version: v1
- Dependency: authority
- Result: fail
- Failure Mode: trust-anchor mismatch
- Drill Window: 2025-01-15T19:40:00Z..2025-01-15T20:00:00Z
- Evidence Path: reports/drills/sim-auth-001.json
- Automated Failover Triggered: yes
- Automated Failback Completed: no
- Trend Window Size: 20
- Trend Segment: tenant=tenant-a, dependency=authority
- Trend Sample Count: 14
- Trend Fail Count: 6
- Trend Pass Count: 8
- Trend Fail Rate: 42%

### Suggested Action
Enforce strict trust-chain validation and block scorecard publication
until failover authority verification passes.

### Metadata
- Source: outage_simulation
- Tags: outage-simulation, authority, policy-tuning

---

## [REC-OUT-20250115-SIMAUTH001] policy_tuning

**Logged**: 2025-01-15T20:10:00Z
**Source Simulation**: SIM-AUTH-001
**Tenant ID**: tenant-a
**Rollout Phase**: r3
**Policy Profile**: org-dr-sentinel
**Dependency**: authority
**Outcome**: fail
**Status**: suggested

### Recommendation
Set `hooks.engine.attestationRevocationMode=strict`, keep
`hooks.engine.authorityOutageSimulationEnabled=true`, and require
validated `hooks.engine.authorityFailoverRunbookId` evidence before
phase promotion.

### Scoring
- Recommendation Score: 80
- Severity: critical
- Trend Window Size: 20
- Trend Segment: tenant=tenant-a, dependency=authority
- Trend Sample Count: 14
- Trend Fail Count: 6
- Trend Pass Count: 8
- Trend Fail Rate: 42%
- Weight Source: assets/policy-profile-scoring-weights.csv
- Weight Schema Version: v1
- Strict Schema Gate: v1
- Manifest Source: assets/policy-profile-scoring-weights.manifest
- Manifest Version: 1
- Signed By: governance-signing-service
- Signed At: 2026-03-26T00:00:00Z
- Key Id: org-governance-key-v1
- Manifest Signature: attest:v1:org-governance-ca:2026-03-26
- Strict Manifest Gate: true

### Target Controls
- hooks.engine.attestationRevocationMode
- hooks.engine.authorityOutageSimulationEnabled
- hooks.engine.authorityFailoverRunbookId

### Evidence
- Drill Report: reports/drills/sim-auth-001.json

---
```

## Validation Gate Failure Example

```text
$ scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-001 --tenant-id tenant-a --rollout-phase r2 --policy-profile missing-profile --dependency registry --result pass --evidence-path reports/drills/sim-reg-001.json
Missing required policy profile 'missing-profile' in: ./blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.csv
```

```text
$ scripts/outage-outcome-promoter.ps1 --dry-run --simulation-id SIM-AUTH-002 --tenant-id tenant-a --rollout-phase r3 --policy-profile org-dr-sentinel --strict-schema-version v2 --dependency authority --result fail --evidence-path reports/drills/sim-auth-002.json
Schema version mismatch for profile 'org-dr-sentinel': expected 'v2' but found 'v1' in ./blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.csv
```

```text
$ scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-003 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --require-signed-manifest --manifest-file ./blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest --dependency registry --result pass --evidence-path reports/drills/sim-reg-003.json
Signed manifest integrity failure: weights_sha256 mismatch for ./blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.csv
```

## Learning: Knowledge Gap (Resolved)

```markdown
## [LRN-20250115-002] knowledge_gap

**Logged**: 2025-01-15T14:22:00Z
**Priority**: medium
**Status**: resolved
**Area**: config

### Summary
Project uses pnpm not npm for package management

### Details
Attempted to run `npm install` but project uses pnpm workspaces.
Lock file is `pnpm-lock.yaml`, not `package-lock.json`.

### Suggested Action
Check for `pnpm-lock.yaml` or `pnpm-workspace.yaml` before assuming npm.
Use `pnpm install` for this project.

### Metadata
- Source: error
- Related Files: pnpm-lock.yaml, pnpm-workspace.yaml
- Tags: package-manager, pnpm, setup

### Resolution
- **Resolved**: 2025-01-15T14:30:00Z
- **Commit/PR**: N/A - knowledge update
- **Notes**: Added to AGENTS.md for future reference

---
```

## Learning: Promoted to Workspace Guidance

```markdown
## [LRN-20250115-003] best_practice

**Logged**: 2025-01-15T16:00:00Z
**Priority**: high
**Status**: promoted
**Promoted**: TOOLS.md
**Area**: backend

### Summary
API responses must include correlation ID from request headers

### Details
All API responses should echo back the X-Correlation-ID header from
the request. This is required for distributed tracing. Responses
without this header break the observability pipeline.

### Suggested Action
Always include correlation ID passthrough in API handlers.

### Metadata
- Source: user_feedback
- Related Files: src/middleware/correlation.ts
- Tags: api, observability, tracing

---
```

## Error Entry

```markdown
## [ERR-20250115-A3F] docker_build

**Logged**: 2025-01-15T09:15:00Z
**Priority**: high
**Status**: pending
**Area**: infra

### Summary
Docker build fails on M1 Mac due to platform mismatch

### Error
```
error: failed to solve: python:3.11-slim: no match for platform linux/arm64
```

### Context
- Command: `docker build -t myapp .`
- Dockerfile uses `FROM python:3.11-slim`
- Running on Apple Silicon (M1/M2)

### Suggested Fix
Add platform flag: `docker build --platform linux/amd64 -t myapp .`
Or update Dockerfile: `FROM --platform=linux/amd64 python:3.11-slim`

### Metadata
- Reproducible: yes
- Related Files: Dockerfile

---
```

## Feature Request

```markdown
## [FEAT-20250115-001] export_to_csv

**Logged**: 2025-01-15T16:45:00Z
**Priority**: medium
**Status**: pending
**Area**: backend

### Requested Capability
Export analysis results to CSV format

### User Context
User runs weekly reports and needs to share results with non-technical
stakeholders in Excel. Currently copies output manually.

### Complexity Estimate
simple

### Suggested Implementation
Add `--output csv` flag to the analyze command. Use standard csv module.
Could extend existing `--output json` pattern.

### Metadata
- Frequency: recurring
- Related Features: analyze command, json output

---
```
