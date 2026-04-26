# PORTING_PLAN: web-browsing

## Upstream

- Compare with `openclaw/skills/` web browsing / fetch skills when evolving behavior.

## BlazeClaw differences

- BlazeClaw-specific manifests and scripts under this folder; WebView2 and gateway tool surfaces may differ from upstream Node daemon assumptions.

## Validation

- Added automated parity/e2e regression coverage for ordered workflow behavior in `BlazeClawMfc/tests/ParityCoverageTests.cpp`.
- Runtime required-tool registration diagnostics are validated via parity and lifecycle capability tests.
- Manual chat replay remains recommended for environment-specific network behavior and skill log inspection.
