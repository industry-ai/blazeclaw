# PORTING_PLAN: humanizer

## Upstream

- Align with OpenClaw skill intent: natural-language rewrite of AI-sounding text (`openclaw/skills/` if a matching package exists).

## BlazeClaw differences

- Local adapter `humanizer.rewrite` for ordered content-polishing chains; `_meta.json` and `.clawhub/origin.json` for catalog metadata.
- Phase 3 downstream guardrails implemented in runtime adapter path:
  - low-confidence detection when 2+ critical fields are missing (`Time/Location/People`),
  - deterministic recovery pass from `Core request` via summarize extractor,
  - instruction-artifact sanitation (`去 AI 化`/`调用`/`发送给` and similar control text),
  - explicit confirmation scaffold output when extraction remains low confidence.

## Validation

- Runtime catalog lists tool; exercise via chat orchestration with `summarize` / email flows per `skills/readme.md`.
- Added regression tests in `BlazeClawMfc/tests/ToolRuntimeRegistryIntegrationTests.cpp` for low-confidence scaffold and recovery-from-core-request behavior.
