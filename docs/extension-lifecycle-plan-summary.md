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

How to test locally:
- Build: msbuild blazeclaw/BlazeClaw.sln /p:Configuration=Debug /p:Platform=x64
- Run and inspect gateway state directory for extension_execpath_issues.log when missing execPath present.

Next steps:
- Implement lifecycle state machine completion (`loaded`/`active`/`failed`/`deactivated`).
- Add plugin runtime host bridge so runtime executors bind on activation.
- Move Lobster-class workflow execution to plugin runtime boundaries.
- Add restart-safe approval token/session recovery for `resume`.
- Integrate telemetry into structured app telemetry pipeline.
- Add further validation for executables (permissions, probes).

Milestone alignment:
- Detailed plan: `docs/openclaw-parity-chat-analysis.md` section `2.1.2`.
- Execution breakdown: `docs/extension-lifecycle-plan.md`.
- Runtime contract guardrails: `docs/runtime-plugin-contract.md`.
