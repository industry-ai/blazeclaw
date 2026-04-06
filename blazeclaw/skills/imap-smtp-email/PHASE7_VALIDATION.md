# Phase 7 Manual Validation Checklist

## Scope
Manual verification scenarios for IMAP/SMTP skill operability after full BlazeClaw port integration.

## Prerequisites
- Build output available from `msbuild BlazeClaw.sln`.
- Email config saved via WebView2 (`email.config.open`) to canonical path:
  - `~/.config/imap-smtp-email/.env`
- Tool runtime dependencies installed (`node`, npm packages from skill setup).

## Scenario A - IMAP unread check and fetch
1. Trigger IMAP unread check via tool runtime path (`imap_smtp_email.imap.check`).
2. Verify response contains unread candidates or empty list with no runtime error.
3. Fetch one UID using `imap_smtp_email.imap.fetch`.
4. Confirm payload includes sender, subject, snippet/body, and UID metadata.

Expected:
- Command completes with `status=ok`.
- Chat/output surface shows concise tool lifecycle status and result visibility.

## Scenario B - Attachment download in allowed dir
1. Ensure `.env` includes `ALLOWED_WRITE_DIRS` and target directory is inside allowlist.
2. Run `imap_smtp_email.imap.download` with UID and allowed output directory.
3. Repeat with a disallowed directory.

Expected:
- Allowed path succeeds and writes attachment files.
- Disallowed path fails with explicit access-denied style error.

## Scenario C - SMTP send/test with valid and invalid credentials
1. Run `imap_smtp_email.smtp.test` with valid credentials.
2. Run `imap_smtp_email.smtp.send` to a test recipient.
3. Temporarily set invalid password and re-run test/send.

Expected:
- Valid credentials succeed (`status=ok`).
- Invalid credentials fail with clear, actionable error (no secret leakage).

## Scenario D - Multi-account routing via --account
1. Configure at least one named account prefix in `.env`.
2. Execute IMAP and SMTP commands with `--account <name>`.
3. Execute with malformed account name and with missing account.

Expected:
- Named account routes correctly when configured.
- Invalid account format is rejected early.
- Missing account fails fast with account-not-found style message.

## Scenario E - Config UI roundtrip
1. Open config from chat bridge entry (`email.config.open` or `blazeclaw.chat.email.config.open`).
2. Verify config form auto-loads previously saved values on initialization.
2. Save updated values from WebView2 form.
3. Reopen config UI and verify fields rehydrate from persisted config.
4. Run one IMAP and one SMTP tool command using saved values.

Expected:
- Save succeeds and returns path/ok response.
- Initial open preloads current persisted values.
- Reopen reflects persisted values.
- Runtime tools use updated values without manual file edits.

## Scenario F - End-to-end chat smoke
1. Send a chat prompt that triggers IMAP read flow.
2. Send a chat prompt that triggers SMTP send flow.
3. Observe output pane for multiline payload stability.

Expected:
- Tool-call lifecycle statuses are visible in chat output.
- No output pane instability under multiline/large responses.
- Task deltas remain queryable for the run ids.
