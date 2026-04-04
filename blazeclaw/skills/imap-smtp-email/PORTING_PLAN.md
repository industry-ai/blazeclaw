# IMAP/SMTP Email Skill Full Porting Plan (OpenClaw -> BlazeClaw)

## Objective
Fully port `openclaw/skills/imap-smtp-email/` into `blazeclaw/skills/imap-smtp-email/` with no runtime dependency on OpenClaw and make the skill usable from BlazeClaw chat flows.

## Source Baseline (OpenClaw)
Port these assets and behaviors from:
- `openclaw/skills/imap-smtp-email/SKILL.md`
- `openclaw/skills/imap-smtp-email/_meta.json`
- `openclaw/skills/imap-smtp-email/package.json`
- `openclaw/skills/imap-smtp-email/setup.sh`
- `openclaw/skills/imap-smtp-email/scripts/config.js`
- `openclaw/skills/imap-smtp-email/scripts/imap.js`
- `openclaw/skills/imap-smtp-email/scripts/smtp.js`
- `.clawhub/origin.json` (only if still needed by BlazeClaw packaging conventions)

## Target Integration Areas (BlazeClaw)
Primary integration points to implement and validate:
- Skill packaging/layout under `blazeclaw/skills/imap-smtp-email/`
- MFC configuration entry points and document/view ownership:
  - `CBlazeClawMFCView`
  - `CBlazeClawMFCApp::m_pChatDocTemplate`
  - `CBlazeClawMFCDoc`
- Tool registration and discovery path via gateway tool catalog/registry:
  - `blazeclaw/BlazeClawMfc/src/gateway/GatewayToolRegistry.*`
  - `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`
- Agent/chat execution flow where tools are invoked during chat runs:
  - `gateway.agents.run` execution path
  - task-delta emission path for tool call visibility
- Chat UX/output surface for showing tool-call outcomes:
  - `blazeclaw/BlazeClawMfc/src/app/OutputWnd.cpp` and related chat output plumbing

## Constraints
- Fully port into BlazeClaw workspace (no runtime reads or execution from `openclaw/`).
- Preserve security behavior in email skill:
  - path allowlists (`ALLOWED_READ_DIRS`, `ALLOWED_WRITE_DIRS`)
  - account-scoped credentials with default + prefixed multi-account support
- Keep compatibility with existing gateway protocol shapes for tool preview/execute.
- Validate with `msbuild BlazeClaw.sln`.

## Work Plan

### Phase 1 - Prepare Skill Skeleton and Artifacts
Status: Completed

- [x] Create `blazeclaw/skills/imap-smtp-email/` directory tree.
- [x] Copy and adapt baseline files (`SKILL.md`, scripts, setup, metadata, package).
- [x] Update any naming/vendor text from OpenClaw-specific branding to BlazeClaw where required.
- [x] Ensure scripts remain directly executable and path-safe on Windows/PowerShell usage patterns.

Phase 1 artifact snapshot:
- `blazeclaw/skills/imap-smtp-email/SKILL.md`
- `blazeclaw/skills/imap-smtp-email/_meta.json`
- `blazeclaw/skills/imap-smtp-email/package.json`
- `blazeclaw/skills/imap-smtp-email/setup.sh`
- `blazeclaw/skills/imap-smtp-email/scripts/config.js`
- `blazeclaw/skills/imap-smtp-email/scripts/imap.js`
- `blazeclaw/skills/imap-smtp-email/scripts/smtp.js`
- `blazeclaw/skills/imap-smtp-email/.clawhub/origin.json`

### Phase 2 - Align Skill Manifest and Tool Surface
Status: Completed

- [x] Define BlazeClaw-compatible skill/tool manifest for:
  - IMAP commands (`check`, `fetch`, `search`, `download`, `mark-read`, `mark-unread`, `list-mailboxes`, `list-accounts`)
  - SMTP commands (`send`, `test`, `list-accounts`)
- [x] Confirm tool IDs are stable and unique in gateway tool registry.
- [x] Define argument contracts (JSON payload schema) for each exposed tool operation.

Phase 2 artifact snapshot:
- `blazeclaw/skills/imap-smtp-email/tool-manifest.json`
- `blazeclaw/skills/imap-smtp-email/tool-contracts.json`
- `blazeclaw/skills/imap-smtp-email/TOOL_SURFACE.md`

### Phase 2.5 - Add MFC Email Configuration UI (WebView2)
1. Add an email configuration event entry in `CBlazeClawMFCView` to launch the config flow.
2. Open a new chat document via `CBlazeClawMFCApp::m_pChatDocTemplate` when the configuration event is triggered.
3. Host a WebView2 surface in the new document/view and load `blazeclaw/skills/imap-smtp-email/config.html`.
4. Implement bidirectional bridge between WebView2 JavaScript and native MFC handlers for submit/cancel/validation events.
5. Validate SMTP parameters in native layer (server, port, account, password, TLS flags) before persistence.
6. Persist configuration through `CBlazeClawMFCDoc`-associated storage and map values to skill `.env` format.
7. Add secure persistence protections for password/secret fields and avoid plaintext exposure in logs/UI traces.

### Phase 3 - Implement Runtime Execution Bridge
1. Add runtime executor bindings so tool IDs map to process execution of skill scripts.
2. Implement command argument normalization from gateway JSON into script CLI parameters.
3. Capture stdout/stderr and convert to `ToolExecuteResult` / `ToolExecuteResultV2` with consistent status mapping.
4. Add timeout, cancellation, and error-shape handling compatible with current gateway response format.

### Phase 4 - Integrate with Chat Agent Flow
1. Wire tool-call path from `gateway.agents.run` to runtime tool execution.
2. Emit task delta entries (`toolName`, `argsJson`, `resultJson`, status, timing) for each tool call.
3. Ensure chat flow can interleave model output and tool results without breaking existing run lifecycle.
4. Ensure idempotency behavior remains correct for repeated requests.

### Phase 5 - Configuration and Security Hardening
1. Ensure `.env` lookup behavior works in BlazeClaw environment:
   - primary: `~/.config/imap-smtp-email/.env`
   - fallback: skill-local `.env` (if retained)
2. Ensure MFC WebView2 configuration UI writes to the same canonical config location used by runtime scripts.
3. Preserve strict file access checks for body/attachment read and attachment write operations.
4. Add defensive validation for missing credentials and invalid account names.
5. Verify no secrets are logged in plain text through gateway/output panes.

### Phase 6 - Chat UX and Operability
1. Add/confirm chat-visible status lines for tool execution states (queued/running/success/error).
2. Add UX entry point in chat for opening email configuration UI from `CBlazeClawMFCView`.
3. Ensure tool errors are surfaced in concise, actionable format in chat output.
4. Confirm output pane behavior remains stable for multiline/large payload responses.

### Phase 7 - Testing and Validation
1. Add unit/integration tests for:
   - command mapping and argument parsing
   - runtime executor status/error conversion
   - tool registry exposure and preview/exists/count behavior
   - MFC event -> new doc/template -> WebView2 `config.html` load path
   - WebView2 form submit -> native validation -> `CBlazeClawMFCDoc` config persistence
2. Add gated/manual validation scenarios:
   - unread check, fetch by UID, attachment download in allowed dir
   - SMTP send/test with valid and invalid credentials
   - multi-account routing via `--account`
   - configuration UI open/save/reopen roundtrip and persisted values reflected by runtime tools
3. Build validation:
   - run `msbuild BlazeClaw.sln`
4. Smoke test full chat path with at least one IMAP read flow and one SMTP send flow.

## Deliverables Checklist
- [x] `blazeclaw/skills/imap-smtp-email/` contains full skill artifacts (scripts/docs/meta/setup/package)
- [x] BlazeClaw-compatible tool manifest is defined for IMAP/SMTP operations
- [x] JSON payload argument contracts are defined for all exposed tool operations
- [ ] `blazeclaw/skills/imap-smtp-email/config.html` is integrated into an MFC WebView2 configuration flow
- [ ] `CBlazeClawMFCView` can trigger email configuration and open a new chat document through `m_pChatDocTemplate`
- [ ] WebView2 form values are validated and persisted through `CBlazeClawMFCDoc`-associated config storage
- [ ] BlazeClaw gateway can discover email tools in catalog
- [ ] BlazeClaw runtime can execute IMAP/SMTP skill tools from chat
- [ ] Tool-call deltas are persisted and observable
- [ ] Security allowlist controls are enforced
- [ ] Build and tests pass

## Risks and Mitigations
- **Risk:** Gateway currently has seeded/mock run behavior in `gateway.agents.run`.
  - **Mitigation:** Introduce non-breaking execution branch for tool-enabled runs while keeping fallback path.
- **Risk:** Cross-platform script invocation differences (bash/node/powershell).
  - **Mitigation:** Centralize process-launch abstraction and add Windows-focused invocation tests.
- **Risk:** Credential leakage in logs/output.
  - **Mitigation:** Redact known sensitive fields before telemetry/output emission.
- **Risk:** WebView2-to-native bridge can introduce input-validation and injection issues.
  - **Mitigation:** Validate and sanitize all incoming form payloads in native MFC handlers before file write.
- **Risk:** Tool schema drift between skill CLI and gateway JSON contract.
  - **Mitigation:** Version and test argument-contract adapters.

## Definition of Done
The port is complete when BlazeClaw can execute IMAP and SMTP operations through chat-invoked tools using only `blazeclaw/skills/imap-smtp-email/`, with passing build/tests and no OpenClaw runtime dependency.