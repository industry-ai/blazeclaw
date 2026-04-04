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

## Namespace and Uniqueness

- Namespace prefix is fixed: `imap_smtp_email.`
- Tool IDs are stable and deterministic to avoid drift across runtime and docs.
- Tool IDs are aligned with BlazeClaw gateway naming conventions and are
  intended to be unique in runtime registration.
