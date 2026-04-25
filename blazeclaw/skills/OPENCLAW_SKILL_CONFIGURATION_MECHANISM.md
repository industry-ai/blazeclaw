# OpenClaw Skill Configuration Mechanism (Reference)

This document records how `openclaw/` configures skills and presents
configuration UI, for later BlazeClaw integration/reference work.

## 1) Skill configuration metadata model

OpenClaw skills are declared by `SKILL.md` with frontmatter metadata.

- Required frontmatter keys:
  - `name`
  - `description`
- Common OpenClaw metadata location:
  - `metadata.openclaw`
- Frequently used metadata fields:
  - `emoji`
  - `requires.bins`
  - `install[]` entries (`id`, `kind`, `formula`/etc., `bins`, `label`)

Example pattern is visible in skills like:

- `openclaw/skills/1password/SKILL.md`
- `openclaw/skills/weather/SKILL.md`

## 2) Runtime status + configuration surface contract

OpenClaw macOS UI does not rely on per-skill embedded HTML config pages.
Instead it uses a schema/status-driven settings surface powered by gateway
methods.

### Core gateway methods used by the UI

- `skills.status`
- `skills.install`
- `skills.update`

Client-side typed wrappers are in:

- `openclaw/apps/macos/Sources/OpenClaw/GatewayConnection.swift`

UI models for this payload are in:

- `openclaw/apps/macos/Sources/OpenClaw/SkillsModels.swift`

Notable status model fields include:

- identity and source:
  - `name`, `skillKey`, `source`, `filePath`, `baseDir`
- requirements and missing state:
  - `requirements.bins/env/config`
  - `missing.bins/env/config`
- config and install helpers:
  - `primaryEnv`
  - `configChecks[]`
  - `install[]`
- eligibility toggles:
  - `eligible`, `disabled`, `always`

## 3) How configuration UI is presented in OpenClaw

OpenClaw presents configuration from a central macOS settings view:

- `openclaw/apps/macos/Sources/OpenClaw/SkillsSettings.swift`

Behavior summary:

1. UI loads skills list via `skills.status` (`SkillsSettingsModel.refresh`).
2. Each skill row shows readiness/missing requirements/config checks.
3. Missing env values are edited through a sheet (`EnvEditorView`).
4. Saving env input calls `skills.update` with:
   - `apiKey` for primary key flows, or
   - `env` dictionary for generic environment values.
5. Enable/disable operations also use `skills.update`.
6. Install actions use `skills.install` and then refresh.

So, OpenClaw’s configuration UI is a unified settings experience, not a
skill-local `config.html` rendering model.

## 3.1 Windows presentation behavior in OpenClaw

Based on repository/app-target inspection and OpenClaw README guidance:

- OpenClaw does **not** provide a dedicated native Windows desktop settings UI
  equivalent to macOS `SkillsSettings.swift` in this repository.
- Windows usage is documented as CLI-first, via onboarding wizard and gateway
  workflows (README notes Windows support via WSL2 is recommended).
- Therefore, on Windows, skill configuration is presented primarily through:
  - CLI onboarding/config commands, and
  - direct config/env file workflows (depending on skill),
  not through a Windows-native per-skill GUI in the OpenClaw repo.

Practical implication:

- macOS: centralized in-app settings UI + gateway `skills.*` APIs.
- Windows: CLI/file-driven configuration path (no dedicated native skills UI
  surface found in this codebase).

## 4) How skill configuration is persisted (example: imap-smtp-email)

For `imap-smtp-email`, scripts use `.env` with canonical + fallback lookup.

From:

- `openclaw/skills/imap-smtp-email/scripts/config.js`

Lookup order:

1. Primary: `~/.config/imap-smtp-email/.env`
2. Fallback: skill-local `.env`

`setup.sh` writes/manages canonical `.env` and supports:

- first-time setup
- reconfigure default account
- add/overwrite named accounts (prefix model like `WORK_...`)
- permission tightening (`chmod 600`)

From:

- `openclaw/skills/imap-smtp-email/setup.sh`

## 5) Configuration UI mechanism vs BlazeClaw mechanism

OpenClaw:

- Centralized native settings UI (`SkillsSettings.swift`)
- Gateway-driven config updates (`skills.update`)
- No required per-skill `config.html` host path in the macOS app

BlazeClaw (current implementation path):

- Skill-local WebView2 `config.html` can be opened from skill selection
- Native bridge messages handle save/load and persist to canonical `.env`
- Gateway `skills.update` / `gateway.skills.update`: **`SkillsGatewayMethodHandler::HandleSkillsUpdate`**
  in `blazeclaw/BlazeClawMfc/src/core/SkillsGatewayMethodHandler.h` is the **single** parse/validate/response
  entry; `GatewayHost` forwards protocol frames only (no duplicate param parsing in the dispatcher).

## 6) Reuse guidance for BlazeClaw evolution

If BlazeClaw wants OpenClaw-like behavior in future:

1. Keep `SKILL.md` metadata compatibility (`metadata.openclaw` and/or mapped
   `metadata.blazeclaw`).
2. Prefer a gateway status model with explicit `requirements/missing/configChecks`.
3. Expose a unified `skills.update` path for env/apiKey writes.
4. Keep canonical `~/.config/<skill>/.env` persistence with safe fallback.
5. Maintain UI-level redaction/secure handling for secret values.

Current BlazeClaw recommended target mechanism is documented in:

- `BLAZECLAW_OPENCLAW_ORIGINAL_CONFIG_MECHANISM_ANALYSIS.md`

It defines the unified WebView2 config host model (custom page + generated
fallback) as the preferred Windows-first path.

## Source files reviewed

- `openclaw/apps/macos/Sources/OpenClaw/SkillsModels.swift`
- `openclaw/apps/macos/Sources/OpenClaw/SkillsSettings.swift`
- `openclaw/apps/macos/Sources/OpenClaw/GatewayConnection.swift`
- `openclaw/apps/macos/Sources/OpenClaw/ConfigSettings.swift`
- `openclaw/apps/macos/Sources/OpenClaw/ConfigStore.swift`
- `openclaw/README.md`
- `openclaw/skills/imap-smtp-email/SKILL.md`
- `openclaw/skills/imap-smtp-email/scripts/config.js`
- `openclaw/skills/imap-smtp-email/setup.sh`
- `openclaw/skills/1password/SKILL.md`
- `openclaw/skills/weather/SKILL.md`

## Appendix A) Email tooling finding mechanism parity notes

For inbox-style prompts (for example, "check my inbox", "needs reply within 2 hours"), this document should also be read as a command-dispatch parity reference:

- OpenClaw-style skill invocation relies on frontmatter command dispatch contracts:
  - `command-dispatch: tool`
  - `command-tool: <tool id>`
- BlazeClaw has compatible extraction and invocation plumbing:
  - command metadata extraction: `BlazeClawMfc/src/core/SkillsCommandService.cpp`
  - invocation target resolution: `BlazeClawMfc/src/core/ServiceManager.cpp` (`ResolveSkillInvocationToolTarget`)

Porting implication for `imap-smtp-email`:

1. Keep runtime tool catalog as-is (`imap_smtp_email.imap.*`, `imap_smtp_email.smtp.*`).
2. Add deterministic command-dispatch metadata to `SKILL.md` for inbox triage entrypoints.
3. Add phrase-to-tool aliasing for inbox intents so mailbox prompts resolve to IMAP tools before generic recovery logic.
4. Add tests to keep email-intent resolution anchored to `imap_smtp_email.*`.
