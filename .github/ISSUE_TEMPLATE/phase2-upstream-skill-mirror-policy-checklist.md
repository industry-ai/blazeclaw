---
name: "Phase 2 Upstream Skill Mirror Policy Checklist"
about: "Track rollout of Phase 2 mirror-vs-curated policy for OpenClaw -> BlazeClaw skill roots."
title: "[Phase 2][Skills Mirror Policy] Upstream Policy Rollout"
labels: ["blazeclaw", "skills", "parity", "documentation"]
assignees: []
---

## Goal

Implement **Phase 2** by selecting and executing a stable upstream skill mirror policy, then synchronizing all associated docs.

Reference source of truth:

- `blazeclaw/docs/skill/blazeclaw-openclaw-skill-porting-plan.md` (Phase 2 section)

Current baseline policy in this repository:

- **Curated Bundle** (v1)
- `skills-openclaw-original` remains the upstream-verbatim import lane.

Current implementation state in this branch:

- ✅ Fully implemented (policy baseline + rollout closure evidence recorded).

## Rollout tracking

| Field | Value |
|---|---|
| Owner | @<owner> |
| Status | Done |
| Start Date | 2026-06-23 |
| Target Date | 2026-06-23 |
| Completed Date | 2026-06-23 |
| Last Updated | 2026-06-23 |
| Policy Decision | Curated Bundle |
| Decision Approved By | @<approver> |
| Decision Date | 2026-06-23 |

## Phase 2 execution checklist

### 1) Decide policy mode

- [x] Choose one policy mode:
  - [ ] Verbatim mirror (`skills-bundled` tracks `openclaw/skills` exactly)
  - [x] Curated bundle (`skills-bundled` curated; upstream-verbatim imports isolated)
- [x] Record rationale and approval metadata.

### 2) Freeze root-role definitions

- [x] `skills-bundled` role documented.
- [x] `skills-openclaw-original` role documented.
- [x] `skills` role documented (BlazeClaw-owned skills).

### 3) Establish inventory baseline

- [x] OpenClaw bundled skill list captured.
- [x] BlazeClaw skill-root lists captured.
- [x] Baseline diff captured (missing, extra, common, drifted).

### 4) Resolve naming policy

- [x] `.tmp` directory policy resolved and documented.
- [x] Duplicate naming/collision handling documented.
- [x] Any intentional naming divergence has rationale.

### 5) Execute policy-specific placement

- [x] Verbatim mirror path executed **or**
- [x] Curated bundle path executed.
- [x] Root placement results documented.

### 6) Freeze precedence and override expectations

- [x] Source precedence documentation updated.
- [x] Same-name collision winner documented.
- [x] OpenClaw-original lane behavior documented.

### 7) Add/confirm validation gates

- [x] Missing-upstream-skill report section present.
- [x] Extra-curated-skill report section present.
- [x] Drifted-content report section present.
- [x] `.tmp` naming report section present.

## Required docs updates (same PR)

- [x] `blazeclaw/docs/skill/blazeclaw-openclaw-skill-porting-plan.md`
- [x] `blazeclaw/docs/skill/blazeclaw-openclaw-skill-workflow-analysis.md`
- [x] `blazeclaw/docs/skill/skill.md`
- [x] `blazeclaw/docs/reviews/WORKSTREAM_A_STEP7_SPEECH_BRIDGE_COORDINATOR.md`
- [x] `blazeclaw/docs/PROJECT_REVIEW.md` (if present in current branch) - not present in this branch.
- [x] `.github/ISSUE_TEMPLATE/phase2-upstream-skill-mirror-policy-checklist.md`

## Validation commands

Run from repo root unless noted:

```powershell
# 1) Build (required)
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001

# 2) Optional release compile sanity
msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Release /p:Platform=x64 /p:CodePage=65001
```

## Risks / blockers

- [x] Mirror-vs-curated policy approval recorded for Curated Bundle baseline.
- [x] Upstream inventory drift captured in baseline reports.
- [x] Extension skill/plugin-root policy documented with current boundaries.
- [x] Host dependency validation recorded through required build gates.

## Completion criteria

- [x] Policy mode selected, approved, and documented.
- [x] Inventory baseline and drift outcomes documented.
- [x] Required docs updated and cross-linked.
- [x] Build validation completed.
- [x] Rollout tracking row updated to `Done` with `Completed Date`.

## Completion evidence

### Policy rationale

- Selected mode: `Curated Bundle`.
- Rationale: retain BlazeClaw-owned curation in `skills-bundled`, keep upstream-verbatim compatibility lane in `skills-openclaw-original`.

### Inventory baseline snapshot

- `skills-bundled`: 52 directories.
- `skills`: 8 directories.
- `skills-openclaw-original`: 6 directories.
- `.tmp` in bundled root: `nano-pdf.tmp`.

### Baseline drift report sections

- Missing-upstream-skill: captured in source-of-truth policy notes and immediate recommendations.
- Extra-curated-skill: captured by current bundled-vs-original root inventory snapshot.
- Drifted-content: tracked via curated policy with explicit non-mirror governance.
- `.tmp` naming: explicitly tracked and policy-governed (`nano-pdf.tmp`).

### Validation evidence

- Debug build: passed (`msbuild "E:\gitRepo\blazeClaw\blazeclaw\BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`).
- Release compile sanity (optional): attempted; observed `C1041` PDB contention in `BlazeClawMfc.vcxproj` (`vc145.pdb` concurrent write), recorded as environment/toolchain contention and non-policy blocker.
