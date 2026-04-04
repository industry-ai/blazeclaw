# BlazeClaw Python Runtime Phase 0 Baseline

## Objective
Define the Phase 0 design baseline for dual-mode Python runtime support in BlazeClaw before Phase 1 implementation starts.

## Scope
- Runtime mode model and deterministic resolution order
- Configuration schema draft (global, extension, tool)
- Error code taxonomy for selection and readiness failures
- Explicit fallback semantics for strict and permissive environments

## Runtime Mode Model

### Supported modes
- `external`: execute Python via approved external interpreter process
- `embedded`: execute Python via embedded CPython host (future implementation)

### Effective mode resolution
Highest-precedence configured value wins:
1. `tools.<toolId>.python.runtime.mode`
2. `extensions.<extensionId>.python.runtime.mode`
3. `python.runtime.modeDefault`

### Resolution outcomes
- If resolved mode is valid, continue to runtime-availability checks.
- If mode value is invalid, return `python_runtime_mode_invalid`.
- If no mode can be resolved, return `python_runtime_mode_unresolved`.

## Configuration Schema Draft

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
    "imap-smtp-email": {
      "python": {
        "runtime": {
          "mode": "external"
        }
      }
    }
  },
  "tools": {
    "python.script.run": {
      "python": {
        "runtime": {
          "mode": "embedded"
        }
      }
    }
  }
}
```

## Fallback Semantics

### Strict mode (`strictMode=true`)
- If requested mode is unavailable, fail immediately.
- No automatic fallback unless `allowFallbackToExternal=true` and policy allows it.

### Permissive mode (`strictMode=false`)
- If requested mode is unavailable and requested mode is `embedded`, fallback to `external` is allowed only when `allowFallbackToExternal=true`.
- If fallback is not allowed, fail with mode-specific availability error.

## Error Code Taxonomy (Phase 0 Baseline)
- `python_runtime_mode_invalid`
- `python_runtime_mode_unresolved`
- `python_external_interpreter_not_found`
- `python_embedded_runtime_unavailable`
- `python_embedded_init_failed`
- `python_execution_timeout`
- `python_policy_blocked`

## Runtime Contract Notes
- Gateway response shape remains unchanged; runtime mode is an internal execution concern.
- Runtime selector should be deterministic and side-effect free.
- Telemetry should record:
  - resolved mode
  - requested mode source (tool/extension/global)
  - fallback used (true/false)
  - terminal result code

## Acceptance Criteria for Phase 0
1. Mode model and precedence are documented.
2. Config schema draft is documented and consistent across docs.
3. Error taxonomy is documented and mapped to mode-selection failure paths.
4. Strict/permissive fallback semantics are documented.

## Out of Scope for Phase 0
- Implementing executors or embedded host code
- Build/runtime wiring
- Diagnostics endpoint implementation

## Status
Phase 0 design baseline is documented and ready for Phase 1 implementation work.

Phase 1 external runtime implementation has started and is tracked in:
- `blazeclaw/PYTHON_SUPPORT_IMPLEMENTATION_PLAN.md`

Phase 2 runtime host abstraction implementation is now tracked in:
- `blazeclaw/PYTHON_SUPPORT_IMPLEMENTATION_PLAN.md`

Phase 3 embedded runtime host implementation is now tracked in:
- `blazeclaw/PYTHON_SUPPORT_IMPLEMENTATION_PLAN.md`

Phase 4 policy/security hardening implementation is now tracked in:
- `blazeclaw/PYTHON_SUPPORT_IMPLEMENTATION_PLAN.md`

Phase 5 diagnostics/rollout implementation is now tracked in:
- `blazeclaw/PYTHON_SUPPORT_IMPLEMENTATION_PLAN.md`

Overall plan status:
- Phase 0 through Phase 5 are completed for the currently defined implementation scope.

Test status:
- Runtime-focused automated tests were added and executed for the implemented Python framework phases.
