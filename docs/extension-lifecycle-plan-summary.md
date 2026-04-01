Summary: Extension lifecycle manager & execPath validation

This document records implemented changes related to extension lifecycle activation and execPath validation.

Key points:

- Extension manifests are discovered by ExtensionLifecycleManager::LoadCatalog. Manifests may include an optional `execPath` field.
- During ActivateAll(), the manager registers declared tools into GatewayToolRegistry and validates presence of `execPath` if supplied.
- If `execPath` is missing or file not found, activation proceeds but diagnostics are emitted:
  - OutputDebugStringA() message for immediate debugging.
  - Lightweight telemetry line appended to `extension_execpath_issues.log` in gateway state directory (best-effort).

- Lobster executor binding: When the lobster extension manifest provides an `execPath`, the lifecycle manager
  binds `LobsterExecutor` as the runtime executor for the `lobster` tool during activation.

- ExecPath validation hardening: Activation now canonicalizes execPath, verifies existence, and
  only allows execPaths located under either the extension manifest directory or the gateway state
  directory. Invalid or out-of-root execPaths are logged to `extension_execpath_issues.log` and
  do not result in executor binding.

- Unit tests: Added Catch2 unit tests exercising `ExtensionLifecycleManager::ActivateAll` to verify
  correct binding behavior for allowed and disallowed execPath cases.

- Lifecycle state machine completion: `ExtensionLifecycleManager` now tracks explicit states
  (`discovered`, `loaded`, `active`, `failed`, `deactivated`), returns activation/deactivation
  result records, enforces deterministic activation order, and deactivates in reverse order.
- Activation conflict handling: duplicate enabled tool ids across extensions are now rejected at
  activation time; the conflicting extension is moved to `failed` with code `duplicate_tool_id`.
- Plugin host/runtime abstraction parity: lifecycle activation now calls explicit runtime
  load/resolve operations via `PluginHostAdapter`; deactivation unloads extension runtimes,
  and host-inline extension registrations were removed from `GatewayHost` for lobster/weather/email.
- Lobster workflow runtime hardening: `LobsterExecutor` now uses explicit runtime settings
  (timeout/output cap/argument cap/cwd policy), deterministic process outcome mapping,
  and normalized envelope handling with robust parseable-JSON suffix extraction.
- Approval persistence hardening: `ApprovalTokenStore` now supports typed sessions with TTL
  metadata and restart-safe recovery checks; governance remediation flow issues persisted
  approval tokens and validates invalid/expired/orphaned token outcomes deterministically.
- Concrete ops-tools runtime executors are now adapter-backed via lifecycle activation
  (`weather.lookup`, `email.schedule`) instead of host-inline registrations.
- Chat runtime orchestration now routes weather+scheduled-email prompt class through
  tool execution chain (`weather.lookup` -> report -> `email.schedule prepare`) and
  preserves `chat.events.poll` delta/final semantics.
- UI parity update: chat WebView now renders tool lifecycle timeline/cards from
  `blazeclaw.gateway.tools.lifecycle` payloads (start/result/error/approval phases)
  while retaining existing debug lifecycle status lines.
- Telemetry parity update: gateway now emits structured telemetry envelopes for
  lifecycle transitions, tool invoke/complete flow, and approval session/token
  diagnostics (save/load/invalid/expire/resume/suspend) to deterministic debug sink.
- Parity validation update: smoke/test/fixture coverage now includes lifecycle
  activation catalog checks and prompt-level orchestration sequence validation
  for weather+email flows.

How to test locally:
- Build: msbuild blazeclaw/BlazeClaw.sln /p:Configuration=Debug /p:Platform=x64
- Run and inspect gateway state directory for extension_execpath_issues.log when missing execPath present.

Next steps:
- Integrate telemetry into structured app telemetry pipeline.
- Add further validation for executables (permissions, probes).

Milestone alignment:
- Detailed plan: `docs/openclaw-parity-chat-analysis.md` section `2.1.2`.
- Execution breakdown: `docs/extension-lifecycle-plan.md`.
- Runtime contract guardrails: `docs/runtime-plugin-contract.md`.
