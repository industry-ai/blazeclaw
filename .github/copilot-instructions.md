# Copilot Instructions

## Project Guidelines
- Prefer splitting long lines into shorter, readable multiline formatting in source files.
- Use readable, explicit code over overly compact solutions when implementing BlazeClaw/OpenClaw porting changes.
- Fully port OpenClaw features into BlazeClaw to avoid runtime dependency on the OpenClaw project alongside BlazeClaw.
- Use `msbuild` to build `BlazeClaw.sln` for validation instead of generic build commands.
- For fixing email fallback, use Option 5 + Option 6, which includes a dependency preflight health index and configurable fallback policy profiles.
- For BlazeClaw config UX, keep a dual mechanism: use `config.html` provided by the skill when present; otherwise, use the ported OpenClaw schema-based configuration mechanism.

## Debugging Guidelines
- When debugging behavior regressions, validate by actually modifying code and confirming rerun behavior, especially differences between English and Chinese prompts.

## Execution Protocol
- When a plan is created, proceed directly with implementation without asking for permission; continue execution without pausing for confirmation.
- Follow a task-delta decomposition pattern for embedded orchestration, utilizing ordered tool execution metadata and LLM-driven dynamic tool-call sequencing, avoiding hardcoded flow-specific orchestration logic.
- Do not hard-code ordered-request phrase checks; use structural orchestration signals aligned with OpenClaw behavior.