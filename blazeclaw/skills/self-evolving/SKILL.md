---
name: self-evolving
description: "Captures learnings, errors, and feature requests for continuous improvement in BlazeClaw sessions."
---

# Self-Evolving Skill

Log learnings and errors to markdown files for continuous improvement. Important patterns can be promoted into workspace guidance files.

## Quick Reference

| Situation | Action |
|-----------|--------|
| Command/operation fails | Log to `.learnings/ERRORS.md` |
| User corrects you | Log to `.learnings/LEARNINGS.md` with category `correction` |
| User wants missing feature | Log to `.learnings/FEATURE_REQUESTS.md` |
| Knowledge was outdated | Log to `.learnings/LEARNINGS.md` with category `knowledge_gap` |
| Found better approach | Log to `.learnings/LEARNINGS.md` with category `best_practice` |
| Outage simulation drill completes | Run `scripts/outage-outcome-promoter.*` with tenant + phase + policy profile inputs to log scored recommendations |

## Skill Layout

- `.learnings/` — runtime logs and entries
- `assets/` — reusable templates
- `references/` — setup and integration guidance
- `scripts/` — helper scripts
- `hooks/blazeclaw/` — dedicated event-hook adapter artifacts

## Logging Targets

- `blazeclaw/skills/self-evolving/.learnings/LEARNINGS.md`
- `blazeclaw/skills/self-evolving/.learnings/ERRORS.md`
- `blazeclaw/skills/self-evolving/.learnings/FEATURE_REQUESTS.md`
- `blazeclaw/skills/self-evolving/.learnings/POLICY_TUNING_RECOMMENDATIONS.md`
- `blazeclaw/skills/self-evolving/.learnings/OUTAGE_TREND_HISTORY.csv`
- `blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.csv`
- `blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest`
- `blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest.template`
- `blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.conf`
- `blazeclaw/skills/self-evolving/assets/policy-profile-key-revocations.csv`
- `blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.attestation`
- `blazeclaw/skills/self-evolving/assets/policy-profile-revocation-slo.conf`
- `blazeclaw/skills/self-evolving/.learnings/TENANT_TRUST_POLICY_ATTESTATION_DASHBOARD.md`
- `blazeclaw/skills/self-evolving/.learnings/TENANT_TRUST_POLICY_ATTESTATION_HISTORY.csv`
- `blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_ATTESTATION_ANOMALY_HEATMAP.md`
- `blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_AUTO_REMEDIATION_ROUTING.md`
- `blazeclaw/skills/self-evolving/assets/tenant-criticality-tiers.csv`
- `blazeclaw/skills/self-evolving/assets/attestation-anomaly-threshold-tiers.csv`
- `blazeclaw/skills/self-evolving/assets/attestation-anomaly-time-decay-policy.conf`
- `blazeclaw/skills/self-evolving/assets/attestation-anomaly-recurrence-tuning-policy.conf`
- `blazeclaw/skills/self-evolving/assets/attestation-anomaly-seasonal-decomposition-policy.conf`
- `blazeclaw/skills/self-evolving/assets/attestation-anomaly-seasonal-overlay-policy.csv`
- `blazeclaw/skills/self-evolving/assets/attestation-anomaly-causal-clustering-policy.conf`
- `blazeclaw/skills/self-evolving/.learnings/ANOMALY_CAUSAL_HISTORY.csv`
- `blazeclaw/skills/self-evolving/.learnings/SEASONAL_OVERLAY_CANDIDATES.md`

## Outage Simulation Outcome Workflow

Capture outage drill outcomes and translate them into reusable guidance.

1. Run one of the outage promoter scripts after each drill:
   - `scripts/outage-outcome-promoter.sh`
   - `scripts/outage-outcome-promoter.ps1`
2. Provide required metadata:
   - simulation id
   - tenant id
   - rollout phase (`r1|r2|r3|r4`)
   - policy profile (`--policy-profile`, default `default`)
   - dependency (`registry` or `authority`)
   - result (`pass` or `fail`)
   - evidence path
   - optional weights file (`--weights-file`, defaults to
     `assets/policy-profile-scoring-weights.csv`)
   - optional trend window size (`--trend-window-size`, default `20`)
   - optional strict schema version gate (`--strict-schema-version`)
   - optional signed-manifest verification gate:
     - `--manifest-file` (defaults to
       `assets/policy-profile-scoring-weights.manifest`)
     - `--require-signed-manifest` (fail-fast if verification cannot be
       completed)
   - optional cryptographic signature verification mode:
     - `--signature-verification-mode none|kms|sigstore`
     - KMS mode: `--kms-public-key-file <path>`
     - Sigstore mode:
       - `--sigstore-certificate-file <path>`
       - `--sigstore-certificate-identity <identity>`
       - `--sigstore-oidc-issuer <issuer>`
       - optional `--cosign-path <path>`
     - tool dependencies:
       - KMS mode: `openssl`
       - Sigstore mode: `cosign`
   - optional trust-policy distribution and key revocation gates:
     - `--trust-policy-file` (defaults to
       `assets/policy-profile-trust-policy.conf`)
     - `--require-trust-policy`
     - `--revocation-file` (defaults to
       `assets/policy-profile-key-revocations.csv`)
     - `--require-revocation-check`
   - optional trust-policy publication attestation and revocation SLO
     alert gates:
     - `--trust-policy-attestation-file` (defaults to
       `assets/policy-profile-trust-policy.attestation`)
     - `--require-trust-policy-attestation`
     - `--revocation-slo-file` (defaults to
       `assets/policy-profile-revocation-slo.conf`)
     - `--require-revocation-slo`
   - optional tenant attestation aggregation dashboard controls:
     - `--attestation-dashboard-file` (defaults to
       `.learnings/TENANT_TRUST_POLICY_ATTESTATION_DASHBOARD.md`)
     - `--attestation-history-file` (defaults to
       `.learnings/TENANT_TRUST_POLICY_ATTESTATION_HISTORY.csv`)
     - `--attestation-baseline-window` (default `20`)
     - `--attestation-anomaly-threshold-percent` (default `25`)
     - `--require-attestation-baseline-gate`
     - `--disable-attestation-dashboard`
   - optional cross-tenant heatmap and routing controls:
     - `--cross-tenant-heatmap-file` (defaults to
       `.learnings/CROSS_TENANT_ATTESTATION_ANOMALY_HEATMAP.md`)
     - `--auto-remediation-routing-file` (defaults to
       `.learnings/CROSS_TENANT_AUTO_REMEDIATION_ROUTING.md`)
     - `--disable-cross-tenant-heatmap`
   - optional adaptive anomaly threshold calibration controls:
     - `--tenant-criticality-file` (defaults to
       `assets/tenant-criticality-tiers.csv`)
     - `--adaptive-threshold-policy-file` (defaults to
       `assets/attestation-anomaly-threshold-tiers.csv`)
     - `--disable-adaptive-threshold-calibration`
     - `--require-adaptive-threshold-policy`
   - optional time-decay anomaly weighting controls:
     - `--time-decay-policy-file` (defaults to
       `assets/attestation-anomaly-time-decay-policy.conf`)
     - `--time-decay-half-life` (default `5`)
     - `--disable-time-decay-weighting`
     - `--require-time-decay-policy`
   - optional recurrence auto-tuning controls:
     - `--recurrence-tuning-policy-file` (defaults to
       `assets/attestation-anomaly-recurrence-tuning-policy.conf`)
     - `--disable-recurrence-auto-tuning`
     - `--require-recurrence-tuning-policy`
   - optional seasonal recurrence decomposition controls:
     - `--seasonal-decomposition-policy-file` (defaults to
       `assets/attestation-anomaly-seasonal-decomposition-policy.conf`)
     - `--reporting-cycle-length` (default `12`)
     - `--disable-seasonal-recurrence-decomposition`
     - `--require-seasonal-decomposition-policy`
   - optional holiday/event overlay controls:
     - `--seasonal-overlay-policy-file` (defaults to
       `assets/attestation-anomaly-seasonal-overlay-policy.csv`)
     - `--disable-seasonal-overlay-tuning`
     - `--require-seasonal-overlay-policy`
   - optional anomaly-causal clustering controls:
     - `--causal-clustering-policy-file` (defaults to
       `assets/attestation-anomaly-causal-clustering-policy.conf`)
     - `--overlay-candidate-file` (defaults to
       `.learnings/SEASONAL_OVERLAY_CANDIDATES.md`)
     - `--disable-causal-clustering`
     - `--require-causal-clustering-policy`
3. Script outputs:
   - learning promotion candidate appended to `.learnings/LEARNINGS.md`
   - policy tuning recommendation appended to
     `.learnings/POLICY_TUNING_RECOMMENDATIONS.md`
   - tenant-scoped trend history appended to
     `.learnings/OUTAGE_TREND_HISTORY.csv`
   - dependency-segmented trend window metrics for each tenant
   - phase-aware recommendation score and severity using policy profile
     scoring weights
   - fail-fast validation errors when weights file or profile is missing
     or malformed
   - optional fail-fast schema mismatch errors when strict schema gate is
     enabled
   - optional fail-fast signed-manifest integrity errors when manifest
     metadata is missing or weight digest mismatches
   - optional fail-fast cryptographic signature verification errors for
     KMS/Sigstore integration when mode-specific checks fail
   - optional fail-fast trust-policy distribution errors for stale or
     unauthorized key usage
   - optional fail-fast revocation-list errors when manifest key is
     revoked
   - optional fail-fast trust-policy attestation errors when publication
     metadata is missing or digest mismatches
   - optional fail-fast revocation propagation SLO errors when freshness
     thresholds are exceeded
   - optional fail-fast tenant attestation anomaly baseline breaches when
     baseline gate is enabled
   - optional cross-tenant anomaly heatmap entries with tenant severity
     bands
   - optional cross-tenant auto-remediation routing recommendations for
     anomaly severity
   - optional adaptive anomaly threshold calibration outputs using tenant
     criticality tiers
   - optional time-decay weighted anomaly outputs for drift-sensitive
     tenant baselines
   - optional auto-tuned decay-factor outputs derived from tenant
     incident recurrence patterns
   - optional seasonal decomposition outputs to tune decay factors by
     reporting cycle phase
   - optional holiday/event overlay outputs for cross-cycle seasonal
     multiplier tuning
   - optional anomaly-causal clustering outputs for suggested seasonal
     overlay candidates
4. Promote proven recommendations into governance guidance files:
   - `AGENTS.md`
   - `TOOLS.md`
   - `.github/copilot-instructions.md`

## Promotion Targets

When a pattern is proven, promote it to:

- `AGENTS.md` (workflow/process)
- `SOUL.md` (behavior)
- `TOOLS.md` (tool gotchas)
- `MEMORY.md` (durable context)
- `.github/copilot-instructions.md` (editor assistant guidance)

## References

- `references/examples.md`
- `references/hooks-setup.md`
- `references/openclaw-integration.md` (BlazeClaw content in legacy filename)
- `references/hook-governance-policy-template.md`
- `references/hook-rollout-policy-template.md`
- `references/federated-remediation-governance-scorecard-template.md`
- `references/enterprise-policy-attestation-publication-template.md`

## Usage Guide (BlazeClaw)

1. Ensure the skill exists at `blazeclaw/skills/self-evolving/`.
2. Ensure hook adapter artifacts exist:
   - `hooks/blazeclaw/HOOK.md`
   - `hooks/blazeclaw/handler.ts`
3. Start BlazeClaw runtime from repo root.
4. Verify prompt lifecycle injection via diagnostics report field:
   - `hooks.selfEvolvingHookTriggered: true`
   - `skills.selfEvolvingReminderInjected: true` (fallback/legacy visibility)
   - `hooks.reminderState` and `hooks.reminderReason` for transition telemetry

## Rollout Checklist

- Verify `.learnings/*` files are writable.
- Verify reminder appears in prompt lifecycle when skill is eligible.
- Verify scripts run in your shell environment:
  - `scripts/activator.sh`
  - `scripts/activator.ps1`
  - `scripts/error-detector.sh`
  - `scripts/error-detector.ps1`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-001 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.ps1 --dry-run --simulation-id SIM-AUTH-001 --tenant-id tenant-a --rollout-phase r3 --policy-profile org-dr-sentinel --dependency authority --result fail --evidence-path reports/drills/sample-authority.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-002 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --strict-schema-version v1 --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-003 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --strict-schema-version v1 --require-signed-manifest --manifest-file blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-004 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --strict-schema-version v1 --require-signed-manifest --manifest-file blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest --signature-verification-mode kms --kms-public-key-file ./keys/org-governance-public.pem --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-005 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --require-signed-manifest --manifest-file blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest --signature-verification-mode sigstore --sigstore-certificate-file ./keys/org-governance-cert.pem --sigstore-certificate-identity governance-signing-service@example.com --sigstore-oidc-issuer https://token.actions.githubusercontent.com --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-006 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --require-signed-manifest --manifest-file blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest --require-trust-policy --trust-policy-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.conf --require-revocation-check --revocation-file blazeclaw/skills/self-evolving/assets/policy-profile-key-revocations.csv --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-007 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --require-signed-manifest --manifest-file blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest --require-trust-policy --trust-policy-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.conf --require-revocation-check --revocation-file blazeclaw/skills/self-evolving/assets/policy-profile-key-revocations.csv --require-trust-policy-attestation --trust-policy-attestation-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.attestation --require-revocation-slo --revocation-slo-file blazeclaw/skills/self-evolving/assets/policy-profile-revocation-slo.conf --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-008 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --require-signed-manifest --manifest-file blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest --require-trust-policy --trust-policy-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.conf --require-revocation-check --revocation-file blazeclaw/skills/self-evolving/assets/policy-profile-key-revocations.csv --require-trust-policy-attestation --trust-policy-attestation-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.attestation --require-revocation-slo --revocation-slo-file blazeclaw/skills/self-evolving/assets/policy-profile-revocation-slo.conf --attestation-baseline-window 20 --attestation-anomaly-threshold-percent 25 --require-attestation-baseline-gate --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-009 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --require-signed-manifest --manifest-file blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest --require-trust-policy --trust-policy-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.conf --require-revocation-check --revocation-file blazeclaw/skills/self-evolving/assets/policy-profile-key-revocations.csv --require-trust-policy-attestation --trust-policy-attestation-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.attestation --require-revocation-slo --revocation-slo-file blazeclaw/skills/self-evolving/assets/policy-profile-revocation-slo.conf --attestation-baseline-window 20 --attestation-anomaly-threshold-percent 25 --require-attestation-baseline-gate --cross-tenant-heatmap-file blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_ATTESTATION_ANOMALY_HEATMAP.md --auto-remediation-routing-file blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_AUTO_REMEDIATION_ROUTING.md --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-010 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --require-signed-manifest --manifest-file blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest --require-trust-policy --trust-policy-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.conf --require-revocation-check --revocation-file blazeclaw/skills/self-evolving/assets/policy-profile-key-revocations.csv --require-trust-policy-attestation --trust-policy-attestation-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.attestation --require-revocation-slo --revocation-slo-file blazeclaw/skills/self-evolving/assets/policy-profile-revocation-slo.conf --attestation-baseline-window 20 --attestation-anomaly-threshold-percent 25 --require-attestation-baseline-gate --tenant-criticality-file blazeclaw/skills/self-evolving/assets/tenant-criticality-tiers.csv --adaptive-threshold-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-threshold-tiers.csv --require-adaptive-threshold-policy --cross-tenant-heatmap-file blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_ATTESTATION_ANOMALY_HEATMAP.md --auto-remediation-routing-file blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_AUTO_REMEDIATION_ROUTING.md --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-011 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --require-signed-manifest --manifest-file blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest --require-trust-policy --trust-policy-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.conf --require-revocation-check --revocation-file blazeclaw/skills/self-evolving/assets/policy-profile-key-revocations.csv --require-trust-policy-attestation --trust-policy-attestation-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.attestation --require-revocation-slo --revocation-slo-file blazeclaw/skills/self-evolving/assets/policy-profile-revocation-slo.conf --attestation-baseline-window 20 --attestation-anomaly-threshold-percent 25 --require-attestation-baseline-gate --tenant-criticality-file blazeclaw/skills/self-evolving/assets/tenant-criticality-tiers.csv --adaptive-threshold-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-threshold-tiers.csv --require-adaptive-threshold-policy --time-decay-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-time-decay-policy.conf --time-decay-half-life 5 --require-time-decay-policy --cross-tenant-heatmap-file blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_ATTESTATION_ANOMALY_HEATMAP.md --auto-remediation-routing-file blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_AUTO_REMEDIATION_ROUTING.md --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-012 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --require-signed-manifest --manifest-file blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest --require-trust-policy --trust-policy-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.conf --require-revocation-check --revocation-file blazeclaw/skills/self-evolving/assets/policy-profile-key-revocations.csv --require-trust-policy-attestation --trust-policy-attestation-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.attestation --require-revocation-slo --revocation-slo-file blazeclaw/skills/self-evolving/assets/policy-profile-revocation-slo.conf --attestation-baseline-window 20 --attestation-anomaly-threshold-percent 25 --require-attestation-baseline-gate --tenant-criticality-file blazeclaw/skills/self-evolving/assets/tenant-criticality-tiers.csv --adaptive-threshold-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-threshold-tiers.csv --require-adaptive-threshold-policy --time-decay-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-time-decay-policy.conf --require-time-decay-policy --recurrence-tuning-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-recurrence-tuning-policy.conf --require-recurrence-tuning-policy --cross-tenant-heatmap-file blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_ATTESTATION_ANOMALY_HEATMAP.md --auto-remediation-routing-file blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_AUTO_REMEDIATION_ROUTING.md --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-013 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --require-signed-manifest --manifest-file blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest --require-trust-policy --trust-policy-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.conf --require-revocation-check --revocation-file blazeclaw/skills/self-evolving/assets/policy-profile-key-revocations.csv --require-trust-policy-attestation --trust-policy-attestation-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.attestation --require-revocation-slo --revocation-slo-file blazeclaw/skills/self-evolving/assets/policy-profile-revocation-slo.conf --attestation-baseline-window 20 --attestation-anomaly-threshold-percent 25 --require-attestation-baseline-gate --tenant-criticality-file blazeclaw/skills/self-evolving/assets/tenant-criticality-tiers.csv --adaptive-threshold-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-threshold-tiers.csv --require-adaptive-threshold-policy --time-decay-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-time-decay-policy.conf --require-time-decay-policy --recurrence-tuning-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-recurrence-tuning-policy.conf --require-recurrence-tuning-policy --seasonal-decomposition-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-seasonal-decomposition-policy.conf --reporting-cycle-length 12 --require-seasonal-decomposition-policy --cross-tenant-heatmap-file blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_ATTESTATION_ANOMALY_HEATMAP.md --auto-remediation-routing-file blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_AUTO_REMEDIATION_ROUTING.md --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-014 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --require-signed-manifest --manifest-file blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest --require-trust-policy --trust-policy-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.conf --require-revocation-check --revocation-file blazeclaw/skills/self-evolving/assets/policy-profile-key-revocations.csv --require-trust-policy-attestation --trust-policy-attestation-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.attestation --require-revocation-slo --revocation-slo-file blazeclaw/skills/self-evolving/assets/policy-profile-revocation-slo.conf --attestation-baseline-window 20 --attestation-anomaly-threshold-percent 25 --require-attestation-baseline-gate --tenant-criticality-file blazeclaw/skills/self-evolving/assets/tenant-criticality-tiers.csv --adaptive-threshold-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-threshold-tiers.csv --require-adaptive-threshold-policy --time-decay-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-time-decay-policy.conf --require-time-decay-policy --recurrence-tuning-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-recurrence-tuning-policy.conf --require-recurrence-tuning-policy --seasonal-decomposition-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-seasonal-decomposition-policy.conf --reporting-cycle-length 12 --require-seasonal-decomposition-policy --seasonal-overlay-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-seasonal-overlay-policy.csv --require-seasonal-overlay-policy --cross-tenant-heatmap-file blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_ATTESTATION_ANOMALY_HEATMAP.md --auto-remediation-routing-file blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_AUTO_REMEDIATION_ROUTING.md --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-015 --tenant-id tenant-a --rollout-phase r2 --policy-profile org-prod-opa --require-signed-manifest --manifest-file blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest --require-trust-policy --trust-policy-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.conf --require-revocation-check --revocation-file blazeclaw/skills/self-evolving/assets/policy-profile-key-revocations.csv --require-trust-policy-attestation --trust-policy-attestation-file blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.attestation --require-revocation-slo --revocation-slo-file blazeclaw/skills/self-evolving/assets/policy-profile-revocation-slo.conf --attestation-baseline-window 20 --attestation-anomaly-threshold-percent 25 --require-attestation-baseline-gate --tenant-criticality-file blazeclaw/skills/self-evolving/assets/tenant-criticality-tiers.csv --adaptive-threshold-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-threshold-tiers.csv --require-adaptive-threshold-policy --time-decay-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-time-decay-policy.conf --require-time-decay-policy --recurrence-tuning-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-recurrence-tuning-policy.conf --require-recurrence-tuning-policy --seasonal-decomposition-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-seasonal-decomposition-policy.conf --reporting-cycle-length 12 --require-seasonal-decomposition-policy --seasonal-overlay-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-seasonal-overlay-policy.csv --require-seasonal-overlay-policy --causal-clustering-policy-file blazeclaw/skills/self-evolving/assets/attestation-anomaly-causal-clustering-policy.conf --require-causal-clustering-policy --overlay-candidate-file blazeclaw/skills/self-evolving/.learnings/SEASONAL_OVERLAY_CANDIDATES.md --cross-tenant-heatmap-file blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_ATTESTATION_ANOMALY_HEATMAP.md --auto-remediation-routing-file blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_AUTO_REMEDIATION_ROUTING.md --dependency registry --result pass --evidence-path reports/drills/sample-registry.json`
  - `scripts/outage-outcome-promoter.sh --dry-run --simulation-id SIM-REG-001 --tenant-id tenant-a --rollout-phase r2 --policy-profile missing-profile --dependency registry --result pass --evidence-path reports/drills/sample-registry.json` (expect fail-fast error)
  - `scripts/extract-skill.sh --dry-run`
  - `scripts/extract-skill.ps1 --dry-run`
- Verify policy-as-code rollout controls are mapped in
  `references/hook-governance-policy-template.md`.
- Verify federated remediation scorecards are defined from
  `references/federated-remediation-governance-scorecard-template.md`.
- Verify organization-specific policy engine and attestation publication
  workflow is defined from
  `references/enterprise-policy-attestation-publication-template.md`.
- Verify tenant policy registry and centralized attestation authority
  wiring is defined in
  `references/enterprise-policy-attestation-publication-template.md`.
- Verify outage simulation and automated failover/failback runbooks are
  defined in
  `references/enterprise-policy-attestation-publication-template.md`.
- Add team-facing guidance entry in `.github/copilot-instructions.md`.

## Known Limitations

None in current self-evolving runtime scope.

## Follow-Up Enhancements

- Add confidence-weighted causal graphing for multi-factor overlay recommendations.
