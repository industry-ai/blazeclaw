# Hook Setup Guide (BlazeClaw)

Configure self-evolving reminders for coding workflows.

## Current Status

Runtime integration provides reminder injection through BlazeClaw
event-hook execution lifecycle.

The self-evolving hook now runs through BlazeClaw TypeScript runtime
execution for this scope.

This is an adapter model for BlazeClaw runtime (not a direct clone of
OpenClaw's `agent:bootstrap` hook runtime).

You can still use these options for local workflow reinforcement:

1. **Manual reminder pattern** in your workflow documents (`AGENTS.md`, `BOOTSTRAP.md`, `.github/copilot-instructions.md`)
2. **Shell-level hook scripts** via your coding tools (for example Claude Code/Codex local hook settings)
3. **Periodic review task** that prompts logging to `.learnings/`

## Recommended Interim Setup

Add this section to `.github/copilot-instructions.md` in your repo:

```markdown
## Self-Evolving Reminder

After tasks that involve corrections, failures, or new recurring patterns:
- Log learnings to `skills/self-evolving/.learnings/LEARNINGS.md`
- Log failures to `skills/self-evolving/.learnings/ERRORS.md`
- Log missing capabilities to `skills/self-evolving/.learnings/FEATURE_REQUESTS.md`
```

## Optional Local Tool Hooks

If your local CLI supports shell hooks, point them to scripts under:

- `blazeclaw/skills/self-evolving/scripts/activator.sh`
- `blazeclaw/skills/self-evolving/scripts/error-detector.sh`
- `blazeclaw/skills/self-evolving/scripts/activator.ps1`
- `blazeclaw/skills/self-evolving/scripts/error-detector.ps1`
- `blazeclaw/skills/self-evolving/scripts/outage-outcome-promoter.sh`
- `blazeclaw/skills/self-evolving/scripts/outage-outcome-promoter.ps1`

(These scripts are ported in Phase 4.)

## Verification

- Confirm `.learnings/` files exist under `blazeclaw/skills/self-evolving/`
- Confirm your workflow guidance includes explicit logging instructions
- Confirm at least one test entry can be appended without format errors
- Confirm runtime diagnostics include:
  - `hooks.engineMode` with active runtime value (`bun`, `tsx`, `node-ts-node`, or `deno`)
  - `hooks.selfEvolvingHookTriggered: true` when self-evolving hook executed
  - `hooks.hookDispatchCount > 0` during active runs
  - `hooks.hookFailureCount == 0` for healthy dispatch
  - `hooks.reminderState` in `{reminder_triggered, reminder_injected, reminder_skipped, reminder_fallback_used}`
  - `hooks.reminderReason` for transition reason details
  - `hooks.reminderEnabled` and `hooks.reminderVerbosity` for active policy
  - no duplicate `bootstrapFiles` entries when multiple hook packages inject shared paths

## Notes

This document intentionally avoids OpenClaw-specific commands and events.
Phase 5 established hook metadata and handler contract files under:

- `blazeclaw/skills/self-evolving/hooks/blazeclaw/HOOK.md`
- `blazeclaw/skills/self-evolving/hooks/blazeclaw/handler.ts`

Runtime integration activates reminder injection when:

- the `self-evolving` skill is included in eligible prompt skills, and
- `hooks/blazeclaw/HOOK.md` plus `hooks/blazeclaw/handler.ts` are present.

Prompt-side reminder injection is retained only as optional fallback via
`hooks.engine.fallbackPromptInjection`.

Reminder policy controls are configurable via:

- `hooks.engine.reminderEnabled`
- `hooks.engine.reminderVerbosity` (`minimal|normal|detailed`)

Tenant governance automation controls are configurable via:

- `hooks.engine.allowPackage` (repeat per package)
- `hooks.engine.strictPolicyEnforcement`

Governance drift diagnostics:

- `hooks.policyBlocked`
- `hooks.driftDetected`
- `hooks.lastDriftReason`

Governance reporting pipeline controls:

- `hooks.engine.governanceReportingEnabled`
- `hooks.engine.governanceReportDir`

Closed-loop remediation policy controls:

- `hooks.engine.autoRemediationEnabled`
- `hooks.engine.autoRemediationRequiresApproval`
- `hooks.engine.autoRemediationApprovalToken`
- `hooks.engine.autoRemediationTenantId`
- `hooks.engine.autoRemediationPlaybookDir`
- `hooks.engine.autoRemediationTokenMaxAgeMinutes`

Governance reporting diagnostics:

- `hooks.governanceReportsGenerated`
- `hooks.lastGovernanceReportPath`
- `hooks.remediationTelemetryEnabled`
- `hooks.remediationAuditEnabled`
- `hooks.lastRemediationTelemetryPath`
- `hooks.lastRemediationAuditPath`
- `hooks.remediationSloStatus`
- `hooks.remediationSloMaxDriftDetected`
- `hooks.remediationSloMaxPolicyBlocked`
- `hooks.lastComplianceAttestationPath`

Centralized observability and remediation endpoints:

- `gateway.runtime.governance.reportStatus`
- `gateway.runtime.governance.remediationPlan`
- `gateway.runtime.governance.executeRemediation`
  - requires `approved=true` and `approvalToken` when approval gates are enabled

Enterprise telemetry and audit controls:

- `hooks.engine.remediationTelemetryEnabled`
- `hooks.engine.remediationTelemetryDir`
- `hooks.engine.remediationAuditEnabled`
- `hooks.engine.remediationAuditDir`
- `hooks.engine.remediationSloMaxDriftDetected`
- `hooks.engine.remediationSloMaxPolicyBlocked`
- `hooks.engine.complianceAttestationEnabled`
- `hooks.engine.complianceAttestationDir`

Enterprise SLA governance and cross-tenant aggregation controls:

- `hooks.engine.enterpriseSlaGovernanceEnabled`
- `hooks.engine.enterpriseSlaPolicyId`
- `hooks.engine.crossTenantAttestationAggregationEnabled`
- `hooks.engine.crossTenantAttestationAggregationDir`

Organization policy engine and publication controls:

- `hooks.engine.policyEngineProfile`
- `hooks.engine.policyEngineDecisionMode`
- `hooks.engine.attestationPipelineEnabled`
- `hooks.engine.attestationPipelineId`
- `hooks.engine.scorecardPublicationEnabled`
- `hooks.engine.scorecardPublicationPipelineId`

Tenant policy registry and centralized authority controls:

- `hooks.engine.policyRegistryEndpoint`
- `hooks.engine.policyRegistryNamespace`
- `hooks.engine.policyRegistrySyncMode`
- `hooks.engine.attestationAuthorityEndpoint`
- `hooks.engine.attestationTrustAnchorId`
- `hooks.engine.attestationRevocationMode`

Outage simulation and failover controls:

- `hooks.engine.registryOutageSimulationEnabled`
- `hooks.engine.registryFailoverRunbookId`
- `hooks.engine.authorityOutageSimulationEnabled`
- `hooks.engine.authorityFailoverRunbookId`
- `hooks.engine.failbackRequiresApproval`

Enterprise attestation endpoint:

- `gateway.runtime.governance.attestationStatus`
- `gateway.runtime.governance.aggregationStatus`

Governance and rollout templates:

- `references/hook-governance-policy-template.md`
- `references/hook-rollout-policy-template.md`
- `references/federated-remediation-governance-scorecard-template.md`
- `references/enterprise-policy-attestation-publication-template.md`

Federated governance scorecard workflow:

1. Define policy controls in `hook-governance-policy-template.md`.
2. Apply rollout gates in `hook-rollout-policy-template.md`.
3. Generate a federated scorecard from
   `federated-remediation-governance-scorecard-template.md` at each
   rollout phase transition.
4. Define organization policy engines, attestation pipeline, and
   automated publication workflow in
   `enterprise-policy-attestation-publication-template.md`.
5. Wire tenant policy registry mappings and centralized attestation
   authority trust settings using
   `enterprise-policy-attestation-publication-template.md`.
6. Execute outage simulations and validate automated failover/failback
   runbooks using
   `enterprise-policy-attestation-publication-template.md`.
7. Run outage outcome promoter scripts to append learning promotion
   candidates and policy tuning recommendations:
   - `.learnings/LEARNINGS.md`
   - `.learnings/POLICY_TUNING_RECOMMENDATIONS.md`
   - `.learnings/OUTAGE_TREND_HISTORY.csv`

Outage promoter inputs now require tenant and rollout phase context:

- `--tenant-id <tenant-id>`
- `--rollout-phase <r1|r2|r3|r4>`
- `--policy-profile <profile-id>` (`default` if not provided)
- Optional `--weights-file <path>` for custom profile weight definitions.
- Optional `--trend-window-size <n>` for tenant trend scoring windows.
- Optional `--strict-schema-version <version>` to enforce profile schema
  compatibility.
- Optional `--manifest-file <path>` and `--require-signed-manifest` to
  enforce signed-manifest integrity verification.

Fail-fast validation gates:

- Missing weights file causes immediate script failure.
- Missing `--policy-profile` row in weights CSV causes immediate script failure.
- Malformed weight fields (non-numeric or invalid divisors) cause immediate
  script failure.
- When `--strict-schema-version` is set, missing schema column/value or
  version mismatch causes immediate script failure.
- When `--require-signed-manifest` is enabled, missing manifest,
  malformed manifest fields, or digest mismatch causes immediate script
  failure.

Default profile weights are provided in:

- `assets/policy-profile-scoring-weights.csv`

Default signed manifest artifacts are provided in:

- `assets/policy-profile-scoring-weights.manifest`
- `assets/policy-profile-scoring-weights.manifest.template`
