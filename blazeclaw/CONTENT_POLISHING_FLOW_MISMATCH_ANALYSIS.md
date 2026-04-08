# BlazeClaw vs OpenClaw: Content Polishing Flow Mismatch Analysis

## Problem Summary

Given the prompt:

1. read draft text
2. call `summarize`
3. call `humanizer`
4. call `imap-smtp-email`

OpenClaw executes a 3-step skill chain (`summarize` -> `humanizer` -> `imap-smtp-email`).

BlazeClaw instead executes `brave_search.search.web` and fails with:

- `tools.execute.result tool=brave_search.search.web status=invalid_arguments`
- skill-path log: `[SkillPath] brave_search.search.web [requested]` then `[invalid_arguments]`

## Key Findings

### 1) BlazeClaw enables command-tool dispatch for `brave-search`, but OpenClaw does not

- BlazeClaw `brave-search` skill frontmatter includes:
  - `command-dispatch: tool`
  - `command-tool: brave_search.search.web`
  - file: `blazeclaw/skills/brave-search/SKILL.md`
- OpenClaw `brave-search` skill does **not** include these dispatch fields:
  - file: `openclaw/skills/brave-search/SKILL.md`

This creates a BlazeClaw-only dispatch bias toward Brave Search in command-oriented prompts.

### 2) BlazeClaw only builds embedded `toolBindings` from dispatch-enabled commands

- In BlazeClaw chat runtime callback, `toolBindings` are created only when `command.dispatch.enabled == true`:
  - file: `blazeclaw/BlazeClawMfc/src/core/ServiceManager.cpp` (around lines 4356-4371)

Because `summarize` and `humanizer` skills do not expose tool dispatch metadata, they are not represented in `toolBindings` for embedded execution.

### 3) Embedded dynamic tool loop selects tools by string-position matching in prompt/message

- Tool planning uses `ResolveNextToolFromPromptWindow` and `ResolveDynamicExecutionPlan`:
  - file: `blazeclaw/BlazeClawMfc/src/core/PiEmbeddedService.cpp` (around lines 276-347)
- It ranks candidates by first substring hit of command/tool names in the combined `skillsPrompt + message` window.

This heuristic can over-select whichever dispatch tool is most explicitly surfaced in planner context (here, Brave Search).

### 4) Brave Search argument build path is strict and rejects unsuitable query payloads

- For `brave_search.search.web`, args require valid string field `query` and safety checks:
  - file: `blazeclaw/BlazeClawMfc/src/core/ServiceManager.cpp` (around lines 2109-2128)
- Embedded run builds search args from quoted fragment or full message:
  - `query = TryExtractQuoted(message).value_or(message)`
  - file: `blazeclaw/BlazeClawMfc/src/core/PiEmbeddedService.cpp` (around lines 817-819)
- `TryExtractQuoted` only checks single quotes (`‘...’` / `'...'`), not double quotes:
  - file: `blazeclaw/BlazeClawMfc/src/core/PiEmbeddedService.cpp` (around lines 146-159)

For long structured prompts, this can pass an oversized/noisy query into Brave Search and trigger `invalid_arguments` (`query failed safety validation`).

## Root Cause (Consolidated)

BlazeClaw-specific dispatch + embedded planning heuristics cause the workflow request to be interpreted as a tool-dispatch candidate, with Brave Search becoming the first executable path. Since the prompt is not a web-search request and produced unsuitable `query` args, `brave_search.search.web` fails validation.

OpenClaw avoids this specific failure path because its `brave-search` skill metadata does not enable this command-tool dispatch behavior.

## Suggestions

### Priority A (low-risk, quick alignment)

1. Remove or gate `command-dispatch` metadata in `blazeclaw/skills/brave-search/SKILL.md`.
2. Keep Brave Search invocable, but do not let it auto-capture generic ordered workflow prompts.

### Priority B (better orchestration quality)

1. Improve ordered-step target resolution to prefer explicit `call <skill>` tokens over broad prompt substring matching.
2. Add a strict allowlist in embedded planning for ordered workflows (execute only explicitly requested targets in order).

### Priority C (argument robustness)

1. Extend quoted-text extraction to support double quotes (`"..."`).
2. For search tools, if extracted query exceeds safety limits, derive a compact query or skip tool call with a clear planner error.

### Priority D (feature parity with OpenClaw behavior)

1. Add dedicated, explicit runtime adapters for `summarize`, `humanizer`, and `imap-smtp-email` ordered chaining.
2. Route `call summarize/humanizer/imap-smtp-email` to deterministic chain execution before general dynamic loop fallback.

## Recommended Next Step

Implement Priority A + B first to stop incorrect Brave Search capture, then add Priority C hardening. This should bring BlazeClaw behavior closer to OpenClaw for the reported prompt pattern while keeping tool dispatch capabilities for genuine search intents.

## Implementation Status

- Priority A has been implemented:
  - Removed brave-search `command-dispatch` metadata from
    `blazeclaw/skills/brave-search/SKILL.md`.
  - Updated associated docs to reflect explicit tool-call usage and disabled
    auto-capture policy for generic ordered workflows.

- Priority B has been implemented:
  - Ordered-step extraction now prefers explicit `call <skill-or-tool>`
    directives over broader inferred structural tokens when available.
  - Enforced ordered workflows now pass a strict allowlist of preflight-
    resolved tool IDs into embedded planning, which executes only the
    allowlisted tools in the resolved order.
  - Associated runtime and skills docs were updated to describe the new
    precedence and strict-allowlist behavior.

- Priority C has been implemented:
  - Quoted-text extraction now supports both single-quote and double-quote
    patterns (including smart quotes) in embedded planning input parsing.
  - Search argument synthesis now derives a compact normalized query for
    search tools when source text is oversized/noisy.
  - If a safe search query cannot be derived, embedded planning now emits a
    clear terminal planner error (`planner_invalid_search_query`) instead of
    relying on downstream opaque invalid-arguments failures.
  - Runtime invalid-arguments recovery for `.search.web` also applies compact
    query derivation before retry.
