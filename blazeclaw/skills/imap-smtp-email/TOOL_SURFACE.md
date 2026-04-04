# IMAP/SMTP Email Tool Surface (Phase 2)

This document defines the BlazeClaw tool IDs, command mapping, and payload contract files for `imap-smtp-email`.

## Artifacts

- `tool-manifest.json`: stable tool catalog entries (`id`, `label`, `category`, `enabled`) plus runtime mapping metadata.
- `tool-contracts.json`: JSON payload schemas for each tool.

## Tool IDs

### IMAP

- `imap_smtp_email.imap.check` -> `node scripts/imap.js check`
- `imap_smtp_email.imap.fetch` -> `node scripts/imap.js fetch`
- `imap_smtp_email.imap.download` -> `node scripts/imap.js download`
- `imap_smtp_email.imap.search` -> `node scripts/imap.js search`
- `imap_smtp_email.imap.mark_read` -> `node scripts/imap.js mark-read`
- `imap_smtp_email.imap.mark_unread` -> `node scripts/imap.js mark-unread`
- `imap_smtp_email.imap.list_mailboxes` -> `node scripts/imap.js list-mailboxes`
- `imap_smtp_email.imap.list_accounts` -> `node scripts/imap.js list-accounts`

### SMTP

- `imap_smtp_email.smtp.send` -> `node scripts/smtp.js send`
- `imap_smtp_email.smtp.test` -> `node scripts/smtp.js test`
- `imap_smtp_email.smtp.list_accounts` -> `node scripts/smtp.js list-accounts`

## JSON Payload Contract Rules

- Payloads are validated against `tool-contracts.json` before CLI argument assembly.
- `account` is optional and maps to `--account <name>`.
- Field-to-flag normalization is required for hyphenated flags:
  - `subjectFile` -> `--subject-file`
  - `bodyFile` -> `--body-file`
  - `htmlFile` -> `--html-file`
- For IMAP UID operations, numeric and string UIDs are accepted.
- For SMTP send, one of `subject` or `subjectFile` is required.

## MFC WebView2 Configuration Bridge (Phase 2.5)

- Open configuration UI event: `blazeclaw.email.config.open`
- Save configuration event: `blazeclaw.email.config.save`
- Cancel configuration event: `blazeclaw.email.config.cancel`
- Save success event: `blazeclaw.email.config.saved`
- Save error event: `blazeclaw.email.config.error`

The WebView2 page `config.html` posts bridge events to the native MFC view.
The native layer validates inputs and persists `.env` content via
`CBlazeClawMFCDoc`-associated storage.
Selecting the `imap-smtp-email` item in `CSkillView` also opens this
`config.html` surface through `CBlazeClawMFCView` skill-selection routing.

## Runtime Execution Bridge (Phase 3)

- Runtime executor bindings are registered at startup through `ServiceManager`.
- `GatewayHost` now exposes runtime tool registration methods used by startup wiring.
- Tool execution invokes `node <script> <command> ...` with normalized CLI args.
- Process stdout/stderr is captured and returned through `ToolExecuteResultV2.result`.
- Deadline-based timeout maps to `status=timed_out` and `errorCode=deadline_exceeded`.
- Argument/launch failures map to structured error codes (for example:
  `invalid_arguments`, `invalid_args_json`, `process_start_failed`).

## Chat Agent Flow Integration (Phase 4)

- `gateway.agents.run` now routes into the `chat.send` runtime flow.
- The resulting run lifecycle is surfaced through `gateway.agents.wait` by
  tracking chat run state, terminal events, and persisted task deltas.
- Tool-call task delta telemetry and persistence remain owned by the chat
  runtime path, avoiding duplicate hardcoded orchestration branches.

## Configuration and Security Hardening (Phase 5)

- Canonical runtime config path: `~/.config/imap-smtp-email/.env`
- Fallback config path: skill-local `.env`
- MFC WebView2 config save path now targets the canonical runtime location.
- `--account` now enforces a safe account-name pattern.
- Missing required IMAP/SMTP credentials fail fast for operational commands.
- File allowlist checks for read/write operations use canonical path
  normalization to reduce symlink and traversal bypass risk.

## Chat UX and Operability (Phase 6)

- Chat bridge supports opening config UI through:
  - channel: `blazeclaw.chat.email.config.open`
  - rpc method: `email.config.open`
- Tool lifecycle states continue emitting concise status lines (`start`,
  `result`, `error`) with actionable detail.
- Tool execution output can be appended as multiline chat status blocks for
  better operator visibility.
- Output pane now guards stability by:
  - splitting multiline payloads into rows
  - truncating oversized single-line entries
  - trimming oldest rows when retention limit is exceeded

## Testing and Validation (Phase 7)

- Automated coverage includes parity tests for:
  - tool catalog discovery (`LoadExtensionToolsFromCatalog`) and preview behavior
  - `gateway.agents.run` / `gateway.agents.wait` lifecycle with task-delta visibility
- Manual validation checklist is documented in:
  - `PHASE7_VALIDATION.md`
- Build and regression validation command:
  - `msbuild BlazeClaw.sln`

## Skill Catalog Navigation (CSkillView)

- `CSkillView::FillSkillView()` consumes `gateway.skills.list` and renders
  registered skills grouped by category.
- `CSkillView` also discovers implemented local skills from
  `blazeclaw/skills/*` (for example `imap-smtp-email`) so implemented skills
  remain visible even before runtime registration completes.
- Selecting a skill item routes its metadata payload to
  `CBlazeClawMFCView::ShowSkillSelection`.
- `CBlazeClawMFCView` surfaces selected skill properties/configuration through:
  - multiline status block output
  - bridge channel payload: `blazeclaw.skills.selection`

## Namespace and Uniqueness

- Namespace prefix is fixed: `imap_smtp_email.`
- Tool IDs are stable and deterministic to avoid drift across runtime and docs.
- Tool IDs are aligned with BlazeClaw gateway naming conventions and are
  intended to be unique in runtime registration.
