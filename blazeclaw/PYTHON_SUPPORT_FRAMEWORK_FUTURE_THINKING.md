# Python Support Framework for BlazeClaw (Future Thinking)

## Why this topic matters
Many OpenClaw workflows rely on Python scripts. BlazeClaw currently executes extension tools through external processes and runtime executors, but it does not yet provide a dedicated Python runtime framework.

## What we observed

### BlazeClaw (current)
- Extension activation is centralized in `ExtensionLifecycleManager`.
- Tool execution is runtime-registered via `GatewayToolRegistry` and executors.
- Existing executors already run external binaries with safety constraints (path checks, timeout/output handling).
- Architecture is friendly to adding a new executor/runtime layer without breaking current gateway contracts.

### OpenClaw (reference)
- Python is mostly handled as an **external interpreter** (for example `python3`) rather than embedded CPython.
- Safety model includes interpreter-aware `safeBins`, profile enforcement, and trusted-directory checks.
- Tooling includes diagnostic/doctor flows to detect risky or incomplete exec policy.

## Is adding Python C API support a good idea?
**Yes, but phased.**

A full embedded CPython runtime is powerful for long-term portability and performance, but it introduces packaging, ABI, lifecycle, and security complexity. A phased framework gives immediate compatibility while keeping a path to deeper integration.

## Can BlazeClaw support both external and embedded modes at the same time?
**Yes. This is the recommended end-state.**

Different skills/modules/tools/extensions can select runtime mode per configuration, as long as they share the same gateway-level execution contract.

Suggested selection model:
- Global default: `python.runtime.modeDefault = external|embedded`
- Per-extension override: `extensions.<id>.python.runtime.mode = external|embedded`
- Per-tool override: `tools.<toolId>.python.runtime.mode = external|embedded`

Resolution order (highest wins):
1. Tool-level mode
2. Extension/module-level mode
3. Global default

Fallback behavior recommendation:
- If configured mode is `embedded` but embedded runtime is unavailable/unhealthy, either:
  - fail closed for strict environments, or
  - fallback to `external` only when explicit policy allows fallback.

## Recommended framework (phased)

## Phase 1 — External Python Runtime Adapter (near term)
- Add a dedicated BlazeClaw Python executor category (for example `python.script.run`).
- Reuse existing process execution patterns (timeouts, bounded output, cwd validation).
- Add interpreter policy settings inspired by OpenClaw:
  - allowed interpreter names/paths
  - trusted script directories
  - per-script profile/allow rules
- Normalize Python execution result envelope to existing tool response schema.

Outcome: fast OpenClaw module porting with minimal architecture risk.

## Phase 2 — Embedded Python Host Abstraction (design now, implement later)
- Introduce an interface boundary, e.g. `IPythonRuntimeHost`:
  - `Initialize(config)`
  - `ExecuteScript(scriptPathOrCode, args, env)`
  - `Shutdown()`
- Provide first implementation as subprocess-backed host (Phase 1 behavior).
- Prepare second implementation using CPython C API (`Python.h`) behind same interface.
- Add a runtime selector that dispatches execution to external or embedded host based on resolved per-tool configuration.

Outcome: no lock-in; future CPython embedding can be swapped in without gateway protocol churn.

## Phase 3 — CPython C API Integration (when needed)
- Embed Python runtime per process/session policy.
- Manage GIL, interpreter lifecycle, and thread affinity explicitly.
- Restrict imports/sys.path and bind BlazeClaw-safe APIs only.
- Add strict observability (startup latency, memory growth, execution faults).

Outcome: reduced process spawn overhead and deeper in-process extensibility.

## Key design guardrails
- Keep tool contracts protocol-stable; hide runtime choice behind executor/host abstraction.
- Enforce least privilege for filesystem/network/environment exposure.
- Prefer deterministic script packaging over ad-hoc inline code for production skills.
- Add feature flags for rollout (`python.runtime.mode = external|embedded`).
- Keep approval/safety policy mode-aware (for example stricter import/path policy in embedded mode).

## Risks to account for
- Python distribution/version management on Windows.
- Native dependency wheels and ABI mismatch.
- GIL contention and crash blast radius in embedded mode.
- Security boundary erosion if interpreter policy is weak.

## Suggested first backlog items
1. Define `PythonRuntimeConfig` schema in BlazeClaw state/config.
2. Implement `PythonProcessExecutor` using existing executor safety controls.
3. Add safe interpreter + trusted-dir policy checks (OpenClaw parity direction).
4. Add gateway diagnostics endpoint for Python runtime health and policy status.
5. Add spike branch for `IPythonRuntimeHost` with placeholder embedded backend.

## Phase 0 baseline artifact
- Phase 0 (design/contracts) is now captured in:
  - `blazeclaw/PYTHON_RUNTIME_PHASE0_BASELINE.md`
- Baseline decisions recorded there include:
  - dual-mode runtime model (`external` and `embedded`),
  - precedence order (tool > extension > global),
  - strict/permissive fallback semantics,
  - initial error code taxonomy.

## Phase 1 implementation snapshot
- External Python runtime path is now implemented via:
  - `blazeclaw/BlazeClawMfc/src/gateway/executors/PythonProcessExecutor.*`
- Runtime registry now includes tool:
  - `python.script.run`
- Current policy and safety coverage:
  - interpreter allowlist,
  - trusted script-root enforcement,
  - optional profile allowlist,
  - timeout and output-size limits,
  - telemetry for execution start/completion.
- Validation status:
  - `msbuild blazeclaw/BlazeClaw.sln /t:Build /p:Configuration=Debug /p:Platform=x64` passes after Phase 1 integration.

## Phase 2 implementation snapshot
- Host abstraction is now implemented under:
  - `blazeclaw/BlazeClawMfc/src/gateway/python/*`
- Runtime dispatch now routes `python.script.run` through selector + host dispatcher.
- Current selector precedence in code:
  1. `args.runtime.mode` (tool-level)
  2. `args.extension.runtime.mode` (extension-level)
  3. `BLAZECLAW_PYTHON_RUNTIME_MODE_DEFAULT` (global-level)
- Current fallback controls:
  - `BLAZECLAW_PYTHON_RUNTIME_STRICT_MODE`
  - `BLAZECLAW_PYTHON_RUNTIME_ALLOW_FALLBACK_TO_EXTERNAL`
  - request overrides under `args.runtime`
- Embedded mode path is intentionally a deterministic unavailable stub in Phase 2,
  with optional fallback to external mode when policy permits.

## Phase 3 implementation snapshot
- Embedded runtime host now executes scripts through dynamic CPython runtime loading:
  - `blazeclaw/BlazeClawMfc/src/gateway/python/EmbeddedPythonRuntimeHost.cpp`
- Embedded lifecycle and thread model in code now includes:
  - one-time interpreter initialization,
  - GIL acquisition/release around script execution,
  - serialized execution lock for deterministic behavior.
- Embedded policy controls now include:
  - trusted script-root enforcement,
  - restricted `sys.path` composition,
  - import allowlist gating via `BLAZECLAW_PYTHON_EMBEDDED_ALLOWED_IMPORTS`.
- Embedded runtime configuration controls currently include:
  - `BLAZECLAW_PYTHON_EMBEDDED_DLL`
  - `BLAZECLAW_PYTHON_EMBEDDED_SYS_PATH`
- Runtime output now uses normalized embedded envelopes with stdout/stderr capture,
  and emits `python.embedded.execute` telemetry events.

## Bottom line
Adding Python support is a good strategic move for OpenClaw-to-BlazeClaw porting. Start with policy-driven external execution for immediate migration value, while designing an abstraction that can later host true Python C API embedding when operational cost is justified.
