# BlazeClaw Python Dual-Mode Runtime Implementation Plan

## Goal
Implement a production-ready Python execution framework in BlazeClaw where each skill/module/tool/extension can choose `external` or `embedded` mode through configuration, while preserving existing gateway/tool contracts.

## Non-Goals (Initial Rollout)
- Full parity for every Python package ecosystem edge case.
- Cross-process hot-reload of embedded runtime.
- Multi-tenant interpreter isolation beyond current BlazeClaw process boundaries.

## Guiding Principles
- Keep protocol contracts stable (`GatewayToolRegistry` result envelopes unchanged).
- Use policy-first execution (allowlist, trusted dirs, approval hooks).
- Make runtime selection deterministic (tool > extension > global).
- Ship in phases with feature flags and safe fallback/strict modes.

## Phase 0 — Design Baseline and Contracts

### Deliverables
1. Runtime mode model and resolution order.
2. Config schema draft for global/extension/tool overrides.
3. Error code taxonomy for runtime selection and failures.

### Proposed Config Shape
```json
{
  "python": {
    "runtime": {
      "modeDefault": "external",
      "allowFallbackToExternal": false,
      "strictMode": true
    }
  },
  "extensions": {
    "<id>": {
      "python": {
        "runtime": {
          "mode": "embedded"
        }
      }
    }
  },
  "tools": {
    "<toolId>": {
      "python": {
        "runtime": {
          "mode": "external"
        }
      }
    }
  }
}
```

### Resolution Rules
1. `tools.<toolId>.python.runtime.mode`
2. `extensions.<id>.python.runtime.mode`
3. `python.runtime.modeDefault`

### Candidate Error Codes
- `python_runtime_mode_invalid`
- `python_runtime_mode_unresolved`
- `python_external_interpreter_not_found`
- `python_embedded_runtime_unavailable`
- `python_embedded_init_failed`
- `python_execution_timeout`
- `python_policy_blocked`

### Phase 0 implementation status

- Completed in docs:
  - Added Phase 0 baseline contract document:
    - `blazeclaw/PYTHON_RUNTIME_PHASE0_BASELINE.md`
  - Finalized runtime resolution precedence:
    - tool override > extension override > global default
  - Finalized config schema draft for global/extension/tool runtime mode selection.
  - Finalized strict/permissive fallback semantics and external fallback gating.
  - Finalized initial error taxonomy for mode selection and runtime readiness failures.

---

## Phase 1 — External Runtime Path (MVP)

### Scope
Implement external Python execution as the first production path using existing BlazeClaw executor patterns.

### Tasks
1. Add `PythonProcessExecutor` under gateway executors.
2. Add interpreter/path policy checks:
   - allowed interpreters
   - trusted script dirs
   - optional per-script profile
3. Add timeout/output bounds and cwd/root enforcement (reuse existing executor guards).
4. Emit normalized tool result envelope.
5. Add telemetry counters for success/failure/timeout/policy-block.

### Candidate File Touchpoints
- `blazeclaw/BlazeClawMfc/src/gateway/executors/*`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayToolRegistry.*`
- `blazeclaw/BlazeClawMfc/src/gateway/ExtensionLifecycleManager.*`
- `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost*.cpp`

---

## Phase 2 — Runtime Host Abstraction

### Scope
Introduce abstraction layer so both external and embedded runtimes share one execution contract.

### Tasks
1. Define `IPythonRuntimeHost`:
   - `Initialize(config)`
   - `Execute(request)`
   - `Shutdown()`
2. Implement `ExternalPythonRuntimeHost` wrapping Phase 1 executor.
3. Add `PythonRuntimeSelector` to resolve mode per request.
4. Add fallback policy handling:
   - strict: fail if selected mode unavailable
   - permissive: fallback to external only if allowed
5. Integrate selector into tool execution pipeline.

### Acceptance
- No gateway schema changes needed.
- Runtime mode choice is observable in telemetry/logs.

---

## Phase 3 — Embedded CPython Host

### Scope
Add optional embedded mode via Python C API behind abstraction.

### Tasks
1. Implement `EmbeddedPythonRuntimeHost` (`Python.h`).
2. Manage initialization/lifecycle and finalization strategy.
3. Add GIL-safe execution boundaries and thread model.
4. Restrict `sys.path` and import surface using policy.
5. Normalize errors into existing result envelope.

### Windows Packaging Tasks
1. Define CPython distribution strategy (bundled vs system discovery).
2. Add runtime dependency checks during startup diagnostics.
3. Define version pinning/compatibility policy.

---

## Phase 4 — Policy & Security Hardening

### Scope
Ensure both modes follow consistent and enforceable security policies.

### Tasks
1. Mode-aware approval policy hooks.
2. Trusted path validation for script/module loading.
3. Optional network and environment-variable restrictions.
4. Deny-by-default behavior for unknown script origins.
5. Structured audit events for policy decisions.

---

## Phase 5 — Diagnostics, UX, and Rollout

### Scope
Operationalize runtime selection and failures for maintainers and users.

### Tasks
1. Add diagnostics endpoint(s):
   - runtime availability
   - active mode
   - last error
2. Add health probes for embedded interpreter state.
3. Add feature flags:
   - `BLAZECLAW_PYTHON_RUNTIME_ENABLED`
   - `BLAZECLAW_PYTHON_EMBEDDED_ENABLED`
4. Progressive rollout:
   - internal dev
   - opt-in extensions
   - broader defaulting

---

## Testing Plan

### Unit Tests
- Mode resolution precedence.
- Policy allow/deny behavior.
- Fallback behavior strict vs permissive.
- Error code mapping.

### Integration Tests
- End-to-end tool run in external mode.
- End-to-end tool run in embedded mode.
- Timeout, cancellation, and large-output handling.
- Invalid config and unavailable runtime paths.

### Regression Tests
- Existing non-Python tools unaffected.
- Existing extension lifecycle activation/deactivation unaffected.

### Validation Command
- `msbuild blazeclaw/BlazeClaw.sln /t:Build /p:Configuration=Debug /p:Platform=x64`

---

## Milestone Exit Criteria

### M1 (External MVP)
- External Python mode works with policy checks and telemetry.

### M2 (Dual-Mode Routing)
- Mode selector + abstraction complete, with deterministic fallback policy.

### M3 (Embedded Preview)
- Embedded host executes approved scripts reliably in controlled environments.

### M4 (Production Readiness)
- Security hardening, diagnostics, tests, and rollout flags complete.

---

## Open Decisions
1. Should embedded runtime be process-global or session-scoped?
2. Should fallback to external be allowed by default?
3. Which Python versions are officially supported on Windows for embedded mode?
4. Should per-tool policy support import allowlists in Phase 1 or Phase 4?

## Phase 0 completion audit

Current state: **completed (design/docs baseline)**.

- Verified deliverables:
  - Runtime mode model and deterministic resolution order.
  - Config schema draft for global/extension/tool overrides.
  - Error taxonomy for runtime selection and readiness failures.
  - Fallback semantics for strict and permissive operation modes.
- Tracking artifact:
  - `blazeclaw/PYTHON_RUNTIME_PHASE0_BASELINE.md`
