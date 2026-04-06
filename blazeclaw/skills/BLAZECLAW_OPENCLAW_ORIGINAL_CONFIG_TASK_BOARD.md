# BlazeClaw Config UI Mechanism Task Board
## Execution Tracking for `BLAZECLAW_OPENCLAW_ORIGINAL_CONFIG_MECHANISM_ANALYSIS.md`

Source plan:
- `blazeclaw/skills/BLAZECLAW_OPENCLAW_ORIGINAL_CONFIG_MECHANISM_ANALYSIS.md`

Status legend:
- `Not Started` | `In Progress` | `Blocked` | `Done`

---

## Ownership model

- **A (App/UI):** `BlazeClawMfc/src/app/*`
- **G (Gateway):** `BlazeClawMfc/src/gateway/*`
- **C (Core):** `BlazeClawMfc/src/core/*`
- **QA:** validation/build/runtime checks

Use explicit owner IDs in rows (example: `A1`, `G1`, `C1`, `QA1`).

---

## Phase 1 — Foundation

| Task ID | Work Item | Module / Files | Owner | Status | Test (How to verify) | Evidence (What to attach) |
|---|---|---|---|---|---|---|
| P1.1 | Implement metadata normalization (`metadata.blazeclaw` + `metadata.openclaw`) | `src/core/ServiceManager.cpp` | C1 | Done | Unit/manual check normalized fields in selection/status payload | `BuildGatewaySkillEntry` now resolves `primaryEnv`, `requiresBins/env/config`, and source tags; payload exposed through `gateway.skills.list` |
| P1.2 | Add canonical config persistence adapter for `.env` and structured JSON | `src/app/BlazeClawMFCDoc.h`, `src/app/BlazeClawMFCDoc.cpp` | A1 | Done | Save/load roundtrip for one env-centric and one config-path skill | Added `SaveSkillConfigEnv`/`LoadSkillConfigEnv`/`GetSkillConfigPath`; email path preserved with fallback compatibility |
| P1.3 | Wire unified bridge contract events in WebView2 host | `src/app/BlazeClawMFCView.h`, `src/app/BlazeClawMFCView.cpp` | A1 | Done | Verify `ready/save/cancel/validate` and `loaded/saved/error/validation` events | Added `HandleSkillConfigBridgeMessage` and load/save/validate/cancel responses using `blazeclaw.skill.config.*` contract |
| P1.4 | Expose normalized selection payload from skill selection flow | `src/app/SkillView.cpp`, `src/app/MainFrame.cpp` | A2 | Done | Select skill and inspect payload fields (`requires.*`, `primaryEnv`) | Skill selection already forwards full gateway skill payload; `gateway.skills.list` now includes normalized metadata fields |

### Phase 1 acceptance gate
- Skill selection shows normalized metadata.
- `ready` returns rehydrated values for `imap-smtp-email`.
- Save returns deterministic `configPath`.

---

## Phase 2 — Unified WebView2 Config Host

| Task ID | Work Item | Module / Files | Owner | Status | Test (How to verify) | Evidence (What to attach) |
|---|---|---|---|---|---|---|
| P2.1 | Implement mode router (Custom `config.html` vs Generated fallback) | `src/app/BlazeClawMFCView.cpp` | A1 | Done | Skill with `config.html` opens custom UI; skill without it opens generated UI | `OpenSkillConfigDocument(...)` now routes to generated `about:blank` host when `config.html` is missing |
| P2.2 | Ensure skill selection always triggers config host entry path | `src/app/SkillView.cpp`, `src/app/MainFrame.cpp` | A2 | Done | Single-click/double-click path consistency in `CSkillView` | `ShowSkillSelection(...)` now passes full properties into open flow, preserving tree-triggered entry behavior |
| P2.3 | Build generated-form bootstrap payload from normalized metadata and path hints | `src/core/ServiceManager.cpp`, `src/app/BlazeClawMFCView.cpp` | C1 | Done | Generated form fields match `requires.env/config` for sampled skills | Added `configPathHints` mapping/serialization and metadata-driven generated form bootstrap |
| P2.4 | Add generated form section grouping and default controls | `src/app/BlazeClawMFCView.cpp` (and related web assets if introduced) | A3 | Done | Form contains Credentials/Connectivity/Policy/Advanced + save/cancel/validate | Generated page includes credentials/config sections and validate/save/cancel controls wired to bridge |

### Phase 2 acceptance gate
- Existing custom config UI still works unchanged.
- Generated fallback works for at least one `openclaw-original` skill without `config.html`.

---

## Phase 3 — Runtime/Status Alignment

| Task ID | Work Item | Module / Files | Owner | Status | Test (How to verify) | Evidence (What to attach) |
|---|---|---|---|---|---|---|
| P3.1 | Extend skills payload with config readiness (`missing`, `install`, eligibility summary) | `src/gateway/GatewayHost.cpp` | G1 | Done | Compare `gateway.skills.list` payload before/after fields | Added missing requirement arrays (`missingEnv/missingConfig/missingBins/missingAnyBins`) and install reason details in skills list/info payloads |
| P3.2 | Add/align `skills.update`-style mutation behavior (env/apiKey/config-path semantics) | `src/gateway/GatewayHost.cpp` | G1 | Done | Execute updates from UI and direct gateway calls; results must match | Added `gateway.skills.update` + `skills.update` alias routed via ServiceManager callback and canonical persistence adapters |
| P3.3 | Refresh skill tree metadata/status immediately after config saves | `src/app/SkillView.cpp`, `src/app/BlazeClawMFCView.cpp` | A2 | Done | Post-save readiness updates without restart | Added `RefreshSkillView()` + `RefreshSkills()` and post-save `gateway.skills.refresh` + tree refresh trigger |
| P3.4 | Enrich tool catalog entries with normalized metadata hints | `src/gateway/GatewayToolRegistry.cpp` | G2 | Done | Inspect `gateway.tools.list/catalog` for expected hint fields | Tool entries now include `skillKey`, `installKind`, and `source`; serialized in gateway tools list/catalog payloads |

### Phase 3 acceptance gate
- Eligibility updates are visible in `CSkillView` without restart.
- Gateway status/update payloads are consistent with UI behavior.

---

## Phase 4 — Hardening and Migration

| Task ID | Work Item | Module / Files | Owner | Status | Test (How to verify) | Evidence (What to attach) |
|---|---|---|---|---|---|---|
| P4.1 | Enforce secret redaction across bridge/status/output surfaces | `src/core/ServiceManager.cpp`, `src/app/BlazeClawMFCView.cpp`, `src/gateway/GatewayHost.cpp` | C1 | Done | Inject secret values and verify no plaintext in logs/panes | Added key-pattern redaction (`pass/secret/token/apiKey`) and truncated sanitized diagnostics payload in skill-config status output |
| P4.2 | Add field-level validation with stable error codes | `src/app/BlazeClawMFCView.cpp`, `src/gateway/GatewayHost.cpp` | A1 | Done | Invalid payload cases return deterministic code + fieldErrors | Added deterministic `code` + `fieldErrors` responses for invalid payload, max fields, invalid key/value lengths, and email field validation failures |
| P4.3 | Add compatibility migration helpers (legacy file reads + optional dual-write) | `src/app/BlazeClawMFCDoc.cpp`, `src/core/ServiceManager.cpp` | C2 | Done | Legacy config import and canonical persistence verification | Added legacy candidate read fallback and optional dual-write via `BLAZECLAW_SKILLS_CONFIG_DUAL_WRITE` |
| P4.4 | Add diagnostics for source-of-truth and migration decisions | `src/app/OutputWnd.cpp` (if needed), `src/app/BlazeClawMFCView.cpp`, `src/core/ServiceManager.cpp` | A3 | Done | Diagnostics show source path and migration action | Added `sourceMeta.sourceOfTruth` + `migratedFromLegacy` and status lines with canonical vs legacy-migrated source path |

### Phase 4 acceptance gate
- No plaintext secret leakage.
- Validation/error behavior is deterministic.
- Legacy-to-canonical migration is reproducible.

---

## Cross-phase validation board

| Check ID | Validation Item | Owner | Status | Command / Procedure | Evidence |
|---|---|---|---|---|---|
| V1 | Build succeeds | QA1 | Not Started | `msbuild blazeclaw/BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64` | Build output (0 errors) |
| V2 | Custom UI skill load/save/rehydrate (`imap-smtp-email`) | QA1 | Not Started | Open config -> modify -> save -> reopen | Screen capture + payload logs |
| V3 | Generated fallback skill load/save/rehydrate | QA1 | Not Started | Open skill without `config.html` -> save -> reopen | Screen capture + payload logs |
| V4 | `openclaw-original` configuration flow works end-to-end | QA2 | Not Started | Select original skill -> configure -> verify readiness | Tree status screenshots + API payload |
| V5 | Security redaction checks | QA2 | Not Started | Execute secret-bearing scenarios | Redaction evidence logs |

---

## Dependency and sequencing notes

- P1.1, P1.2, P1.3 are prerequisites for most of Phase 2.
- P2.1/P2.3 should complete before Phase 3 runtime parity validation.
- Phase 4 should start after Phase 3 status payload stabilization.

---

## Execution log template

Use one entry per task transition.

```text
[YYYY-MM-DD HH:MM] Task=<ID> Status=<new-status> Owner=<owner>
Summary: <what changed>
Tests: <what was run>
Evidence: <links/paths/screenshots/log references>
```

---

## Sign-off checklist

- [ ] All P1 tasks Done + acceptance gate passed
- [ ] All P2 tasks Done + acceptance gate passed
- [ ] All P3 tasks Done + acceptance gate passed
- [ ] All P4 tasks Done + acceptance gate passed
- [ ] V1–V5 validation entries completed with evidence
- [ ] Final summary added to source mechanism doc
