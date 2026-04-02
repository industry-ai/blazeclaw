# OpenClaw vs BlazeClaw Prompt-Orchestration Parity Analysis

## Background
For the same prompt:

> 使用 brave-search multi-search-engine搜索‘今日大模型行业最新动态’，提取前 3 条核心新闻。接着用 Summarize 把它们浓缩成一段 100 字以内的摘要，最后调用 Notion 技能，将摘要写入我的 Notion ‘每日早报’ 页面中。

`openclaw/` decomposes and executes as multiple sequential tool actions, while `blazeclaw/` sends/handles it as one whole message and returns different behavior.

## What I inspected

### BlazeClaw (`blazeclaw/BlazeClawMfc`)
1. `src/gateway/GatewayHost.Handlers.Runtime.cpp`
   - `chat.send` orchestration path only attempts `TryOrchestrateWeatherEmailPrompt(...)`.
   - This orchestration is hardcoded for weather + email flow (`weather.lookup` and `email.schedule`) and not generic skill decomposition.
2. `src/core/ServiceManager.cpp`
   - In `SetChatRuntimeCallback`, when `m_activeChatProvider == "deepseek"`, it directly calls `InvokeDeepSeekRemoteChat(...)`.
   - `InvokeDeepSeekRemoteChat(...)` builds a payload with only one user message and no skill/tool schema loop:
     - `{"model":"...","stream":true,"messages":[{"role":"user","content":"..."}]}`
3. `src/core/PiEmbeddedService.cpp`
   - Current implementation is queue bookkeeping (`QueueRun/CompleteRun`) only.
   - No agent session creation, no tool binding, no iterative tool-call runtime.
4. Skills metadata exists but is not used for runtime orchestration:
   - `SkillsPromptService` / `SkillsCommandService` build snapshots.
   - `gateway.skills.prompt` and `gateway.skills.commands` expose them as state APIs.
   - They are not wired into actual deepseek runtime execution loop.

### OpenClaw (`openclaw/src/agents/pi-embedded-runner/run/attempt.ts`)
1. Embedded runner resolves and injects skills prompt:
   - `resolveEmbeddedRunSkillEntries(...)`
   - `resolveSkillsPromptForRun(...)`
2. Embedded runner builds full toolset:
   - `createOpenClawCodingTools(...)`
3. Embedded runner creates tool-capable agent session:
   - `createAgentSession(...)`
4. Prompt is executed inside session runtime (`activeSession.prompt(...)`) where tool-calls are handled by the session/tool pipeline rather than one-shot raw message forwarding.

## Root cause (missing parity port)
The key missing parity is **not a small prompt-template mismatch**; it is a **runtime capability gap**:

1. BlazeClaw deepseek path is one-shot chat completion transport (raw user content), not a tool-capable embedded agent run.
2. BlazeClaw prompt orchestration currently contains only a specialized weather/email decomposition branch.
3. BlazeClaw has skills prompt/command catalog data, but it is not integrated into a generic multi-step tool execution loop.
4. BlazeClaw `PiEmbeddedService` is currently a lifecycle stub and does not port OpenClaw embedded runner behavior.

This combination explains why your multi-skill prompt (brave-search -> summarize -> notion) stays as one whole request in BlazeClaw.

## Suggestions

### Priority 1 (parity-critical)
1. ✅ Port OpenClaw embedded run pipeline into BlazeClaw runtime path for all chat requests (not only deepseek provider path), including:
   - skill prompt injection,
   - tool registration/binding,
   - iterative tool-call handling until final assistant output.
2. ✅ Replace/augment the direct one-shot payload path so it can participate in tool-execution turns, not only `messages=[user]`.

## Priority 1 implementation update

Implemented in `blazeclaw/BlazeClawMfc`:

1. `src/core/PiEmbeddedService.{h,cpp}` now contains an embedded orchestration adapter:
   - accepts skills prompt + command dispatch bindings + runtime tool catalog,
   - resolves multi-skill intent (including brave-search/summarize/notion pattern),
   - executes tools iteratively through runtime tool executor callbacks,
   - returns ordered tool execution deltas and final assistant output.
2. `src/core/ServiceManager.cpp` `SetChatRuntimeCallback` now:
   - builds runtime command-tool bindings from `SkillsCommandSnapshot`,
   - routes all incoming chat requests through `PiEmbeddedService::ExecuteRun(...)` first,
   - returns embedded orchestration output when handled,
   - falls back to provider runtime when no orchestration match.
3. Provider fallback path now receives injected skills prompt context:
   - deepseek/local fallback prompt uses `[skills_prompt] + [user_message]` composite,
   - deepseek remains model transport backend rather than orchestration owner.
4. `src/gateway/GatewayHost.{h,cpp}` now exposes runtime tool accessors:
   - `ListRuntimeTools()` for tool binding discovery,
   - `ExecuteRuntimeTool(...)` for embedded iterative execution.

Current state: Priority 1 runtime parity gap is closed at BlazeClaw runtime layer; Priority 2/3 items remain follow-up hardening/verification work.

Each tool execution is a task delta with metadata (tool name, args, result) that can be observed in 
order, so we can verify the execution order and results for brave-search -> summarize -> notion test 
case. Each chat request is now routed through the embedded orchestration adapter first, so we can verify 
that the multi-skill prompt is properly decomposed and executed rather than treated as one-shot. Please 
refactor the new embedded orchestration adapter to be as task decomposition and tool execution agnostic 
as possible, so it can be reused for future multi-skill flows without hardcoded patterns. The adapter 
should be able to handle any arbitrary combination of skills/tools as long as they are properly declared 
in the skills prompt and command snapshots. And also we will use llm to discover the decomposition steps 
and tool execution order, so the adapter should be able to support dynamic tool calls rather than fixed 
sequences. The above procedure is called task delta decomposition and it is a common pattern in agent-based 
systems to enable complex multi-step reasoning and tool use. Please create a plan in markdown file to 
track the refactoring and hardening work for the embedded orchestration adapter, including any necessary 
changes to the skills prompt/command schema or runtime tool execution APIs.


### Priority 2 (behavioral parity)
3. Move from specialized `TryOrchestrateWeatherEmailPrompt` to generic skill orchestration (or keep it as fast-path but fallback into generic tool loop).
4. Wire `SkillsCommandSnapshot` + dispatch metadata into runtime execution (not only state reporting endpoint).

### Priority 3 (verification)
5. Add parity test case using your exact Chinese prompt to assert:
   - decomposition steps >= 3,
   - tool execution order includes brave-search then summarize then notion,
   - final response includes Notion write result.

## Practical implementation direction
A minimal-risk path is:
1. Keep existing weather/email orchestrator as-is.
2. For non-matched prompts, route to a new embedded-tool runtime adapter (ported from OpenClaw runner concepts).
3. Reuse existing BlazeClaw skill catalog/eligibility/command snapshots as inputs to that adapter.
4. Keep deepseek transport as model backend only, not as the orchestration owner.

---
If needed, I can next draft a concrete file-level porting checklist (C++ target files + OpenClaw source mapping) for implementation sequencing.