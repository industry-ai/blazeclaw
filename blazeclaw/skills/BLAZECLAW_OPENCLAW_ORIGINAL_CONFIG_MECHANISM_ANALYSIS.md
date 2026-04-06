# BlazeClaw Best Configuration UI Mechanism
## Recommended Standard for all skills (including `openclaw-original`)

## Goal
Define the best long-term configuration UI mechanism for BlazeClaw that:

- is Windows-first and WebView2-native,
- supports all local and `openclaw-original` skills,
- stays compatible with OpenClaw metadata semantics,
- avoids BlazeClaw runtime dependency on OpenClaw app modules,
- scales from simple API-key forms to complex multi-section skill settings.

---

## 1) Final recommendation (decision)

BlazeClaw should adopt a **Unified WebView2 Config Host** with two rendering
modes under one contract:

1. **Mode A — Custom skill page**
   - Use skill-provided `config.html` when present.
2. **Mode B — Generated skill form**
   - If `config.html` is missing, auto-generate UI from normalized metadata and
     known config schemas/path hints.

Both modes must use the same bridge protocol, validation flow, persistence
layer, and status refresh behavior.

This avoids fragmentation, preserves flexibility, and gives immediate coverage
for `openclaw-original` skills.

---

## 2) Why this is the best mechanism for BlazeClaw

## 2.1 Platform fit
- BlazeClaw is MFC/Windows-centric.
- WebView2 gives fast UI iteration and modern forms without custom native
  control complexity.

## 2.2 OpenClaw compatibility fit
- OpenClaw skills rely on metadata semantics (`requires.env`, `requires.config`,
  `primaryEnv`, `install[]`) more than a shared cross-platform HTML UI system.
- A generated fallback preserves compatibility even when no `config.html`
  exists in original skills.

## 2.3 Operational fit
- One bridge contract means less code divergence.
- One persistence policy means deterministic behavior.
- One eligibility refresh path means predictable `CSkillView` status updates.

---

## 3) Required architecture

## 3.1 Metadata normalization layer
Normalize per skill into a BlazeClaw internal config model:

- Input namespaces:
  - `metadata.blazeclaw` (preferred)
  - `metadata.openclaw` (compatibility)
- Output fields:
  - `emoji`
  - `requires.bins`
  - `requires.env`
  - `requires.config`
  - `primaryEnv`
  - `install[]`

Resolution priority:
1. explicit `metadata.blazeclaw`
2. compatible `metadata.openclaw`
3. defaults/derived values

## 3.2 Unified config persistence layer

### Read precedence
1. `~/.config/<skill>/.env` (or canonical JSON file if skill uses structured config)
2. BlazeClaw global structured config store (for config-path requirements)
3. skill-local fallback files
4. optional compatibility aliases (feature-flagged)

### Write policy
- Always write canonical BlazeClaw location first.
- Optional dual-write compatibility mode for migration windows.
- Redact secrets in logs/status output.

## 3.3 Unified WebView2 Config Host

Host responsibilities:
- Resolve selected skill key
- Determine UI mode (custom vs generated)
- Inject bootstrap bridge script
- Send `loaded` payload on ready
- Validate + persist on save
- Return structured result/error messages

---

## 4) Standard bridge contract (mandatory)

## 4.1 Web -> Native
- `blazeclaw.skill.config.ready` `{ skillKey }`
- `blazeclaw.skill.config.save` `{ skillKey, payload }`
- `blazeclaw.skill.config.cancel` `{ skillKey }`
- `blazeclaw.skill.config.validate` `{ skillKey, payload }` (optional)

## 4.2 Native -> Web
- `blazeclaw.skill.config.loaded` `{ skillKey, payload, sourceMeta }`
- `blazeclaw.skill.config.saved` `{ skillKey, configPath, updatedChecks }`
- `blazeclaw.skill.config.error` `{ skillKey, code, message, fieldErrors? }`
- `blazeclaw.skill.config.validation` `{ skillKey, ok, fieldErrors? }`

## 4.3 Behavioral guarantees
- Rehydrate on every `ready`.
- Normalize booleans/numbers/arrays before sending to web UI.
- Keep secret values off telemetry channels and global status panes.

---

## 5) Generated UI fallback rules (Mode B)

When `config.html` is absent:

1. Build fields from `requires.env` and `primaryEnv`.
2. Map `requires.config` paths to known field templates (path registry).
3. Group fields by section:
   - Credentials
   - Connectivity
   - Policy/Safety
   - Advanced
4. Add built-in controls:
   - test/validate button
   - save/cancel
   - last-saved timestamp/source

This makes `openclaw-original` skills configurable without manually authoring
new UI first.

---

## 6) `openclaw-original` compatibility behavior

For skills under `blazeclaw/skills-openclaw-original/*`:

1. Parse `SKILL.md` + `_meta.json`.
2. Normalize OpenClaw metadata.
3. Display capability summary in selection payload.
4. Open config host:
   - custom page if exists,
   - generated page otherwise.
5. Persist via canonical BlazeClaw store.
6. Recompute eligibility and refresh skill status tree.

---

## 7) Windows-specific position

- OpenClaw repository evidence shows macOS native settings UI, but no equivalent
  Windows-native skills settings UI.
- OpenClaw Windows path is effectively CLI/file-driven (README indicates
  Windows usage via WSL2 is recommended).
- Therefore BlazeClaw should explicitly use WebView2 as the primary Windows
  configuration UX baseline.

---

## 8) Security and reliability requirements

Required controls:

1. Secret redaction policy for logs/status/deltas.
2. Field-level validation with explicit error codes.
3. Deterministic persistence path reporting.
4. Input length/type guardrails for bridge payloads.
5. Optional policy gate to block missing required credentials.

---

## 9) Decision matrix (why not alternatives)

1. **Pure native MFC forms per skill**
   - Rejected: high maintenance, low scalability, duplicated UI logic.
2. **Custom HTML only (no generated fallback)**
   - Rejected: poor coverage for many `openclaw-original` skills.
3. **CLI/file-only configuration**
   - Rejected: weak Windows UX and discoverability.

Chosen approach (Unified WebView2 host + dual mode) gives best balance of
coverage, compatibility, and maintainability.

---

## 10) Implementation plan by code module (phased)

This section converts the mechanism into concrete implementation work across
`BlazeClawMfc/src/app`, `src/gateway`, and `src/core`.

## 10.1 Module map (exact files/classes)

### `src/app`
- `blazeclaw/BlazeClawMfc/src/app/SkillView.cpp`
  - class: `CSkillView`
  - role: skill selection payload + category tree status refresh
- `blazeclaw/BlazeClawMfc/src/app/BlazeClawMFCView.cpp`
  - class: `CBlazeClawMFCView`
  - role: WebView2 host, config bridge, custom/generated config UI dispatch
- `blazeclaw/BlazeClawMfc/src/app/BlazeClawMFCView.h`
  - class: `CBlazeClawMFCView`
  - role: bridge API declarations and config host methods
- `blazeclaw/BlazeClawMfc/src/app/BlazeClawMFCDoc.cpp`
  - class: `CBlazeClawMFCDoc`
  - role: canonical config persistence (`.env`/json load-save adapters)
- `blazeclaw/BlazeClawMfc/src/app/BlazeClawMFCDoc.h`
  - class: `CBlazeClawMFCDoc`
  - role: persistence API surface
- `blazeclaw/BlazeClawMfc/src/app/MainFrame.cpp`
  - class: `CMainFrame`
  - role: routing skill-selection/config-open flow into active view

### `src/gateway`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`
  - class: `GatewayHost`
  - role: skills status/update API parity and config check payload contract
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayToolRegistry.cpp`
  - class: `GatewayToolRegistry`
  - role: tool catalog enrichment with normalized metadata/config hints

### `src/core`
- `blazeclaw/BlazeClawMfc/src/core/ServiceManager.cpp`
  - class: `ServiceManager`
  - role: metadata normalization, compatibility resolution,
    secret-redaction policy, and config path/schema registry services

## 10.2 Phase 1 — Foundation (metadata + persistence + bridge baseline)

### Tasks
1. Implement metadata normalization service in `ServiceManager.cpp`:
   - merge `metadata.blazeclaw` + `metadata.openclaw`
   - produce normalized fields (`requires.*`, `primaryEnv`, `install[]`)
2. Add canonical config read/write adapters in `CBlazeClawMFCDoc`
   (`.cpp/.h`):
   - `~/.config/<skill>/.env` support
   - structured json fallback for config-path skills
3. Define and wire the unified bridge event contract in
   `CBlazeClawMFCView` (`.cpp/.h`):
   - `blazeclaw.skill.config.ready/save/cancel/validate`
   - `blazeclaw.skill.config.loaded/saved/error/validation`

### Acceptance criteria
- Selecting a skill exposes normalized metadata payload with resolved
  `requires.*` and `primaryEnv`.
- `ready` returns rehydrated persisted values for at least one existing skill
  (`imap-smtp-email`).
- Save roundtrip persists to canonical location and returns deterministic
  `configPath` in `blazeclaw.skill.config.saved`.

## 10.3 Phase 2 — Unified WebView2 config host (custom + generated)

### Tasks
1. In `CBlazeClawMFCView.cpp`, implement config-host mode router:
   - Mode A: skill `config.html`
   - Mode B: generated fallback form when missing
2. In `MainFrame.cpp` + `SkillView.cpp`, ensure skill selection consistently
   triggers config-host entry flow and status output.
3. Implement generated-form bootstrap payload builder from normalized metadata
   and registry hints (`ServiceManager.cpp`).

### Acceptance criteria
- Skills with `config.html` still open and work unchanged.
- Skills without `config.html` open generated form and allow save.
- `openclaw-original` skill without `config.html` becomes configurable from UI
  without manual file edits.

## 10.4 Phase 3 — Runtime/status alignment and API parity

### Tasks
1. Extend `GatewayHost.cpp` skills payloads with normalized config readiness
   fields:
   - missing env/config checks
   - install options
   - eligibility summary
2. Add `skills.update`-style mutation behavior parity in gateway route handlers
   (where missing): env/apiKey/config-path update semantics.
3. In `SkillView.cpp`, consume refreshed status fields and update tree metadata
   after config saves.
4. In `GatewayToolRegistry.cpp`, carry normalized metadata hints into tool
   catalog entries for discoverability.

### Acceptance criteria
- Post-save eligibility recalculation updates skill readiness in tree without
  app restart.
- Gateway status/list payloads expose consistent config check information.
- UI and gateway update paths produce equivalent mutation outcomes.

## 10.5 Phase 4 — Hardening and migration

### Tasks
1. Enforce secret-redaction policy in `ServiceManager.cpp` and bridge/status
   outputs (`CBlazeClawMFCView.cpp`, gateway status surfaces).
2. Add field-level validation + explicit error codes for generated/custom save
   flows.
3. Add compatibility migration helpers:
   - read legacy skill-local files
   - optional dual-write mode (feature-flagged)
4. Add diagnostics/status lines for source-of-truth and migration decisions.

### Acceptance criteria
- No plaintext secrets are emitted in logs/status panes/tool deltas.
- Invalid payloads return stable error codes + field errors.
- Legacy config values can be imported/read and then persisted to canonical
  store with deterministic behavior.

## 10.6 Cross-phase validation checklist

1. Build validation:
   - `msbuild blazeclaw/BlazeClaw.sln /m /p:Configuration=Debug /p:Platform=x64`
2. Functional validation:
   - Existing custom UI skill (`imap-smtp-email`) load/save/rehydrate
   - Generated fallback skill load/save/rehydrate
   - `openclaw-original` skill configuration flow
3. Runtime validation:
   - skill status refresh in `CSkillView`
   - gateway status/update consistency
4. Security validation:
   - secret redaction checks across output surfaces

---

## 11) Source sample set used in this analysis

- `openclaw/skills/slack/SKILL.md`
- `openclaw/skills/discord/SKILL.md`
- `openclaw/skills/gh-issues/SKILL.md`
- `openclaw/skills/notion/SKILL.md`
- `openclaw/skills/baidu-search/SKILL.md`
- `openclaw/skills/brave-search/SKILL.md`
- `openclaw/skills/openai-image-gen/SKILL.md`
- `openclaw/skills/voice-call/SKILL.md`
- `openclaw/skills/imap-smtp-email/SKILL.md`
- `openclaw/skills/imap-smtp-email/scripts/config.js`
- `openclaw/skills/imap-smtp-email/setup.sh`
- `openclaw/apps/macos/Sources/OpenClaw/SkillsModels.swift`
- `openclaw/apps/macos/Sources/OpenClaw/SkillsSettings.swift`
- `openclaw/apps/macos/Sources/OpenClaw/GatewayConnection.swift`
- `openclaw/apps/macos/Sources/OpenClaw/ConfigStore.swift`
- `openclaw/apps/macos/Sources/OpenClaw/ChannelsStore+Config.swift`
- `openclaw/apps/macos/Sources/OpenClaw/OpenClawConfigFile.swift`
