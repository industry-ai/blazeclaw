# Copilot Instructions

## Project Guidelines
- Prefer splitting long lines into shorter, readable multiline formatting in source files.
- Use readable, explicit code over overly compact solutions when implementing BlazeClaw/OpenClaw porting changes.
- Fully port OpenClaw features into BlazeClaw to avoid runtime dependency on the OpenClaw project alongside BlazeClaw.
- For BlazeClaw changes, ensure strict OpenClaw workflow parity implementation and explicitly avoid hard-coded workflow-specific functions/paths; prefer flexible structural orchestration signals and parity-consistent implementation.
- For BlazeClaw parity work, avoid hard-coded resolver functions like single-city mappings; use flexible OpenClaw-aligned structural signals and generalized extraction logic, and document plan updates in workflow markdowns.
- Use human-readable UTF-8 characters in source code instead of escaped byte sequences in BlazeClaw files.
- Use `msbuild` to build `BlazeClaw.sln` for validation instead of generic build commands. Always validate with: `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`.
- For fixing email fallback, use Option 5 + Option 6, which includes a dependency preflight health index and configurable fallback policy profiles.
- Prefer controlling runtime behavior via `blazeclaw.conf` (config-file control) rather than ad-hoc runtime toggles.

### Speech Recognition
- For BlazeClaw speech recognition work, treat `BlazeClawMfc/blazeclaw.conf` as the current used config file; the active STT path is Sherpa Zipformer (`speech.storageRoot=BlazeClawMfc/models/STT/sherpa-onnx-streaming-zipformer-bilingual-zh-en`), while Qwen3 ASR is obsolete unless explicitly reselected.
- For BlazeClaw Sherpa speech recognition GUI work, use CBlazeClawMFCView as the speech-recognition result/output surface; CChatView may be a recorder provider but should not be treated as the GUI output owner.

### UI & UX
- Target WebView-first UI flows: BlazeClaw currently uses a WebView at BlazeClawMfc/web/chat/index.html via the CBlazeClawMFCView bridge; design parity and UI plans assuming WebView-first implementation.
- Treat CChatView as a potential future UI path; design parity plans to allow migration to CChatView later, but prioritize WebView parity and validation.
- For BlazeClaw config UX, keep a dual mechanism: use `config.html` provided by the skill when present; otherwise, use the ported OpenClaw schema-based configuration mechanism.
- Prefer using scoped RAII guards (Enter/Exit with atomic counter) instead of volatile bool sync flags for guarding re-entrant UI sync operations.

## Code Style
- Use human-readable UTF-8 characters in source text.
- Use readable, explicit code style when doing porting and implementation work.

## Debugging Guidelines
- When debugging behavior regressions, validate by actually modifying code and confirming rerun behavior, especially differences between English and Chinese prompts.

## Execution Protocol
- Execute implementations autonomously in one shot when a plan is created; do not request permission, pause for confirmation, or seek additional approvals during the task — this is the user's stated preference.
- Follow a task-delta decomposition pattern for embedded orchestration, utilizing ordered tool execution metadata and LLM-driven dynamic tool-call sequencing, avoiding hardcoded flow-specific orchestration logic.
- Do not hard-code ordered-request phrase checks; use structural orchestration signals aligned with OpenClaw behavior.
- Use native Sherpa streaming behavior only; explicitly avoid fallback transcription mechanisms.