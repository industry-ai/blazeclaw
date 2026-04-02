# Copilot Instructions

## Project Guidelines
- Prefer splitting long lines into shorter, readable multiline formatting in source files.
- Fully port OpenClaw features into BlazeClaw to avoid runtime dependency on the OpenClaw project alongside BlazeClaw.
- Use `msbuild` to build `BlazeClaw.sln` for validation instead of generic build commands.

## Execution Protocol
- When a plan is created, immediately continue execution without pausing for confirmation; proceed directly through implementation steps.
- Follow a task-delta decomposition pattern for embedded orchestration, utilizing ordered tool execution metadata and LLM-driven dynamic tool-call sequencing, avoiding hardcoded flow-specific orchestration logic.