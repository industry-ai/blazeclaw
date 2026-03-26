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
3. Script outputs:
   - learning promotion candidate appended to `.learnings/LEARNINGS.md`
   - policy tuning recommendation appended to
     `.learnings/POLICY_TUNING_RECOMMENDATIONS.md`
   - tenant-scoped trend history appended to
     `.learnings/OUTAGE_TREND_HISTORY.csv`
   - dependency-segmented trend window metrics for each tenant
   - phase-aware recommendation score and severity using policy profile
     scoring weights
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

- Add policy-profile validation gates that fail fast when required scoring profiles are missing or malformed.
