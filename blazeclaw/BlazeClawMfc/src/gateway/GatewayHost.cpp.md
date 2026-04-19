# GatewayHost — tracking notes

**Related documentation:** `blazeclaw/docs/blazeclaw-openclaw-architecture-framework-gap-analysis.md` (how `GatewayHost` fits the BlazeClaw vs OpenClaw gateway model), `blazeclaw/docs/SERVICE_LAYER_BOUNDARIES.md` (core `ServiceManager` ↔ `GatewayHost` callback boundaries), `blazeclaw/BlazeClawMfc/PROJECT_REVIEW.md` (layering and threading).

This file tracks analysis of `blazeclaw::gateway::GatewayHost` (`GatewayHost.h` / split `.cpp` sources). The section at the bottom preserves earlier **file size** guidance for `GatewayHost.cpp`.

---

## Intended role (from `IGatewayHostRuntime` and usage)

`GatewayHost` implements `IGatewayHostRuntime` (`RouteRequest`, `IsHealthy`) and acts as a **composition root** for the in-process gateway: `GatewayMethodDispatcher`, `GatewayWebSocketTransport`, agent/channel/session/tool registries, extension lifecycle, chat pipeline orchestration, policy guard, event fanout, task-delta persistence, and routing to a staged runtime (`GatewayHostEx`) for selected methods (e.g. `chat.send`). In practice it also exposes **transport I/O**, **event frame builders**, **runtime tool registration**, and many **setter** entry points used by the desktop shell.

### `RegisterDefaultHandlers()` call order (maintain when adding methods)

`GatewayHost::RegisterDefaultHandlers()` delegates to **`GatewayHostRegistration::RegisterDefaultHandlerSequence`** (`GatewayHostRegistrationCoordinator.h` / `.cpp`). That **single** function is a **`friend`** of `GatewayHost` and sequences the private `Register*Handlers` calls with **comment-labelled domain phases** (the calls cannot be split into nested free functions in another TU without losing `friend` access). The **global order** of those private calls is unchanged from the list below.

1. `RegisterChannelsHandlers`
2. `RegisterEventHandlers`
3. `RegisterToolExecutionHistoryHandlers`
4. `RegisterToolsHandlers`
5. `RegisterGeneratedScopeClusterHandlers`
6. `RegisterGatewayEventCatalogQueryHandlers`
7. `RegisterGatewayRegistryIntrospectionHandlers`
8. `RegisterGatewayAgentSessionMutationHandlers`
9. `RegisterGatewayAgentToolSurfaceHandlers`
10. `RegisterGatewayConfigAndDiagnosticsHandlers`
11. `RegisterSecurityOpsHandlers`
12. `RegisterRuntimeHandlers`
13. `RegisterTransportHandlers`
14. `RegisterGatewaySupplementaryCatalogHandlers`

**Domain phases in the coordinator** (comment blocks in `RegisterDefaultHandlerSequence`; each phase is one or more `Register*Handlers` calls, in the order above): channels + event tables → tooling + scope cluster → gateway catalog introspection → agent/session + tool surface → config/diagnostics + security → runtime + transport → supplementary catalog.

Duplicate method names across registrars still overwrite the same dispatcher slot—keep names unique or rely on last registration intentionally.

### `protocol::OkResponse` (success handler boilerplate)

Defined in `GatewayProtocolModels.h`: `OkResponse(const RequestFrame& request, std::string payloadJson)` for the usual case, and `OkResponseOptionalPayload(const RequestFrame& request, std::optional<std::string> payloadJson)` when the body may be omitted (e.g. inline policy skip)—kept as a **separate** function name so string-literal payloads do not hit overload ambiguity with `std::optional`. Handler lambdas return `return protocol::OkResponse(request, …);` instead of spelling out `protocol::ResponseFrame{ .id = request.id, .ok = true, .payloadJson = …, .error = std::nullopt }`.

**Error responses** (`ok == false`, `error` set) should use **`protocol::ErrorResponse`** (`GatewayProtocolModels.h`): `ErrorResponse(request, protocol::ErrorShape{...})`, or short forms `ErrorResponse(request, code, message)` / `ErrorResponse(request, code, message, detailsJson)` / full retry overload. Avoid ad hoc `ResponseFrame{ .id, .ok = false, ... }` in new code.

**Large domain registrars** (`RegisterGatewayRegistryIntrospectionHandlers`, `RegisterGatewayAgentSessionMutationHandlers`, …) are implemented as **`handlers::<family>::*Handlers::RegisterAll(GatewayHost& host)`** in matching `GatewayHost.Handlers.*.cpp` files. `GatewayHost` **`friend`**s those handler structs so `RegisterAll` can wire `host.m_dispatcher.Register(..., [&host](...) { ... })` without exposing private members publicly. `GatewayHost.cpp` only keeps **`RegisterDefaultHandlers`** (coordinator entry) plus non-dispatcher logic.

Workflow for manifest vs static handlers: **`blazeclaw/docs/PROTOCOL_CODEGEN.md`**.

Applied across default gateway handler sources: `GatewayHost.cpp`, `GatewayHost.Handlers.*.cpp`, `GatewayHostCatalogHelpers.cpp`, `GatewayHostModelHelpers.cpp`, `GatewayHostProtocolHelpers.cpp`, and `generated/GatewayHandlerCatalog.Generated.cpp`.

### Table-driven static registrations

Two mechanisms cover **fixed JSON** success handlers:

1. **`RegisterStaticPayloadHandlers` + `StaticPayloadHandlerEntry`** (`GatewayStaticRegistration.h` / `GatewayStaticRegistration.cpp`) — registers a row array `(method name, payload JSON string)` in a loop, each calling `protocol::OkResponse`. Used by `GatewayHost.Handlers.Events.cpp` for the large `gateway.events.*Key` / `*Scope*` static surface (`kEventsStaticPayloadHandlers`). Add rows to that `constexpr` array when introducing new fixed-payload event methods.

2. **`GatewayHandlers.manifest.json` + `Generate-GatewayHandlerCatalog.ps1`** — already table-driven: `kind: "static"` methods emit the same pattern into `generated/GatewayHandlerCatalog.Generated.cpp`; `kind: "toolsMetric"` uses token templates. Prefer the manifest when methods belong to the generated scope-cluster catalog; use the C++ table for ad-hoc static batches (e.g. event key grid) without editing the generator.

### `protocol::EncodeValidatedEvent` (event wire encoding + schema check)

Declared in `GatewayProtocolCodec.h`, implemented in `GatewayProtocolCodec.cpp`:

`EncodeValidatedEvent(std::string eventName, std::string payloadJson, std::uint64_t seq, const std::string& validationStage)`

Builds an `EventFrame` (`stateVersion` = `seq`), runs `GatewayProtocolSchemaValidator::ValidateEvent`, and on failure replaces the frame with `gateway.schema.error` and payload `{"stage":"<validationStage>","message":"event validation failed"}`, then returns `EncodeEventFrame(...)`.

Used by `GatewayHost::Build*EventFrame` methods and by `GatewayEventFanoutService::BuildChatLifecycleEventFrame`. The `validationStage` string must stay JSON-safe (historically alphanumeric / dotted segments); it is inserted into the fallback payload without extra escaping.

### `GatewayJsonSerializers` (registry / task-delta JSON helpers)

Declared in `GatewayJsonSerializers.h`, implemented in `GatewayJsonSerializers.cpp`:

- `EscapeJsonString` — minimal escaping for string values embedded in concatenated JSON (same rules as the former anonymous `EscapeJson` in `GatewayHost.cpp`).
- `SerializeSession`, `SerializeAgent`, `SerializeAgentFile`, `SerializeAgentFileContent`, `SerializeChannelStatus`, `SerializeChannelAccount`, `SerializeChannelRoute`, `SerializeTool`, `SerializeToolExecution`, `SerializeChannelAdapter`, `SerializeStringArray` — stable wire shapes for gateway handler responses.
- `SerializeTaskDeltaEntry`, `SerializeTaskDeltaState` — persistence snapshot JSON for task-delta maps (`TaskDeltaEntry` from `TaskDeltaRepository.h`).

`GatewayHost.cpp` keeps a one-line anonymous wrapper `EscapeJson` → `EscapeJsonString` so existing handler code that still calls `EscapeJson(...)` for ad-hoc payloads is unchanged.

### Lightweight JSON builder (`GatewayJsonBuilder`)

Declared in `GatewayJsonBuilder.h`, implemented in `GatewayJsonBuilder.cpp`. Helpers build **string-table JSON** (not a DOM); string values use `EscapeJsonString` from `GatewayJsonSerializers`.

| API | Role |
|-----|------|
| `JsonString(s)` | JSON string literal (quotes + escape) — **`Telemetry.h`** (`blazeclaw::gateway::JsonString`), included via `GatewayJsonBuilder.h` |
| `JsonBool(b)` | `true` / `false` (`GatewayJsonBuilder.cpp`) |
| `JsonNumber(n)` | Integer (`std::uint64_t` or `std::int64_t`) |
| `JsonObject({{ "key", fragment }, ...})` | Object; each **value** must already be a valid JSON fragment (`JsonString` / `JsonBool` / nested `JsonObject` / `JsonArray` output) |
| `JsonArray({ ... })` / `JsonArray(vector)` | Array of JSON value fragments |

Used in `GatewayHost.cpp` for `BuildModelJson` and `BuildMemorySearchEnvelope` (matches `JsonArray` of per-match objects). The **DeepSeek config object** fragment and **fixed event-name list** for catalog queries live in **`GatewayHostCatalogHelpers.*`** (`BuildGatewayDeepSeekConfigJson`, `GatewayEventCatalogNames`, `MaskGatewaySecret`) so multiple handler TUs share one wire-stable definition.

### `RequestParamsView` (params wrapper)

Declared in `GatewayRequestParams.h`, implemented in `GatewayRequestParams.cpp`. Wraps `request.paramsJson` (the optional raw params object string) with the same behavior as the former `Extract*Param` helpers:

| Method | Notes |
|--------|--------|
| `GetString(name)` | Missing / invalid → empty string |
| `GetBool(name)` | Missing / invalid → `std::nullopt` |
| `GetSize(name)` | Unsigned field via `json::FindUInt64Field` → `std::optional<std::size_t>` |
| `GetObject(name)` | Raw JSON object substring, or nullopt if not an object shape |

Handlers may call `RequestParamsView(request.paramsJson).GetString("field")` inline, or bind once: `const RequestParamsView params(request.paramsJson);` then `params.GetString("channel")`, `params.GetBool("active")`, `params.GetSize("limit")` (see `gateway.config.set`, `gateway.sessions.create`, `gateway.sessions.reset`).

---

## Member functions — grouped by functionality

Counts: **45 public** members + **26 private** members = **71** instance/static methods (excluding nested type definitions).

### 1. Lifecycle and bootstrap

| Member | Notes |
|--------|--------|
| `Start` | Full startup with config |
| `StartLocalOnly` | Local binding variant |
| `StartLocalDispatchOnly` | Dispatcher-only mode |
| `StartLocalRuntimeDispatchOnly` | Runtime dispatch subset |
| `BootstrapCreateRuntimeState` | Phased bootstrap: create state |
| `BootstrapStartRuntimeServices` | Phased bootstrap: services |
| `BootstrapAttachTransportHandlers` | Phased bootstrap: transport |
| `BootstrapStartRuntimeSubscriptions` | Phased bootstrap: subscriptions |
| `BootstrapFinalizeRuntimeInitialization` | Phased bootstrap: finalize |
| `Stop` | Shutdown |
| *(private)* `InitializeRuntime` | Internal init entry |
| *(private)* `CreateRuntimeState` | |
| *(private)* `StartRuntimeServices` | |
| *(private)* `AttachTransportRuntime` | |
| *(private)* `StartRuntimeSubscriptions` | |
| *(private)* `FinalizeRuntimeInitialization` | |

### 2. `IGatewayHostRuntime` and routing

| Member | Notes |
|--------|--------|
| `RouteRequest` | Override; special-cases `chat.send` via `GatewayHostRouter` + optional `GatewayHostEx`, else legacy dispatcher |
| `IsHealthy` | Override; currently tied to `m_dispatchInitialized` |
| *(private)* `RouteRequestLegacy` | `m_dispatcher.Dispatch(request)` |

### 3. Protocol handler registration (dispatcher seeding)

| Member | Notes |
|--------|--------|
| *(private)* `RegisterDefaultHandlers` | **Coordinator only** — calls the domain registrars below in order; lives in `GatewayHost.cpp` |
| *(private)* `RegisterToolExecutionHistoryHandlers` | `gateway.tools.executions.*` (list, count, latest, clear) — `GatewayHost.Handlers.ToolExecutionHistory.cpp` (`handlers::tool_execution::ToolExecutionHistoryHandlers`) |
| *(private)* `RegisterGatewayEventCatalogQueryHandlers` | Event catalog queries + `gateway.tools.categories` — `GatewayHost.Handlers.EventCatalogQuery.cpp` (`handlers::event_catalog_query::EventCatalogQueryHandlers`; **`friend`** on `GatewayHost` for `gateway.config.snapshot` runtime fields) |
| *(private)* `RegisterGatewayRegistryIntrospectionHandlers` | Agents/sessions/tools/models/config/transport “probe” handlers, agent files, models list, `gateway.tools.call.execute` — `GatewayHost.Handlers.RegistryIntrospection.cpp` (`handlers::registry_introspection::RegistryIntrospectionHandlers::RegisterAll`; **`friend`**) |
| *(private)* `RegisterGatewayAgentSessionMutationHandlers` | Agent CRUD/run/wait, session lifecycle, `gateway.features.list`, channel status/route — `GatewayHost.Handlers.AgentSessionMutation.cpp` (`handlers::agent_session_mutation::AgentSessionMutationHandlers::RegisterAll`; **`friend`**) |
| *(private)* `RegisterGatewayAgentToolSurfaceHandlers` | `gateway.agents.get`, `gateway.tools.catalog` / `call.preview`, `gateway.agents.activate` — `GatewayHost.Handlers.AgentToolSurface.cpp` (`handlers::agent_tool_surface::AgentToolSurfaceHandlers::RegisterAll`; **`friend`**) |
| *(private)* `RegisterGatewayConfigAndDiagnosticsHandlers` | `gateway.config.get` / `set`, `gateway.logs.tail`, session resolve/create/reset, `gateway.health` — `GatewayHost.Handlers.ConfigDiagnostics.cpp` (`handlers::config_diagnostics::ConfigDiagnosticsHandlers::RegisterAll`; **`friend`**) |
| *(private)* `RegisterGatewaySupplementaryCatalogHandlers` | Remaining catalog-style methods (`gateway.session.list`, `gateway.events.*`, `gateway.tools.*`, `gateway.models.*`, `gateway.config.*`, logs, health details) — `GatewayHost.Handlers.SupplementaryCatalog.cpp` (`handlers::supplementary_catalog::SupplementaryCatalogHandlers::RegisterAll`; **`friend`**) |
| *(private)* `RegisterChannelsHandlers` | `GatewayHost.Handlers.Channels.cpp` |
| *(private)* `RegisterEventHandlers` | `GatewayHost.Handlers.Events.cpp` |
| *(private)* `RegisterToolsHandlers` | `GatewayHost.Handlers.Tools.cpp` |
| *(private)* `RegisterScopeClusterHandlers` | `GatewayHost.Handlers.ScopeCluster.cpp` |
| *(private)* `RegisterGeneratedScopeClusterHandlers` | `generated/GatewayHandlerCatalog.Generated.cpp` |
| *(private)* `RegisterSecurityOpsHandlers` | `GatewayHost.Handlers.SecurityOps.cpp` |
| *(private)* `RegisterRuntimeHandlers` | `GatewayHost.Handlers.Runtime.cpp` (very large) |
| *(private)* `RegisterTransportHandlers` | `GatewayHost.Handlers.Transport.cpp` |

### 4. Transport and inbound/outbound pumping

| Member | Notes |
|--------|--------|
| `AcceptConnection` | |
| `PumpInboundFrame` | |
| `DrainOutboundFrames` | |
| `PumpNetworkOnce` | |
| `HandleInboundText` | Text-oriented inbound path |

### 5. Push / lifecycle event frame builders

| Member | Notes |
|--------|--------|
| `BuildTickEventFrame` | |
| `BuildHealthEventFrame` | |
| `BuildShutdownEventFrame` | |
| `BuildChannelsUpdateEventFrame` | |
| `BuildChannelsAccountsUpdateEventFrame` | |
| `BuildSessionResetEventFrame` | |
| `BuildAgentUpdateEventFrame` | |
| `BuildToolsCatalogUpdateEventFrame` | |

### 6. In-process runtime tools (tool registry surface)

| Member | Notes |
|--------|--------|
| `ListRuntimeTools` | |
| `ExecuteRuntimeTool` | |
| `ExecuteRuntimeToolV2` | |
| `RegisterRuntimeTool` | |
| `RegisterRuntimeToolV2` | |

### 7. Task delta persistence (backing chat/runtime)

| Member | Notes |
|--------|--------|
| *(private)* `LoadPersistedTaskDeltas` | |
| *(private)* `PersistTaskDeltas` | `const`; persists via repository state |

### 8. Dependency injection / shell wiring (callbacks and snapshot state)

| Member | Notes |
|--------|--------|
| `SetSkillsCatalogState` | |
| `SetSkillsRefreshCallback` | |
| `SetSkillsUpdateCallback` | Delegates skills.update handling to bound implementation |
| `SetConfigSchemaGetCallback` | |
| `SetConfigSchemaLookupCallback` | |
| `SetEmbeddedOrchestrationPath` | Routing / orchestration mode |
| `SetEmailFallbackRuntimeFlags` | |
| `SetEmailFallbackResolvedPolicy` | |
| `SetChatRuntimeCallback` | |
| `SetChatAbortCallback` | |
| `SetEmbeddingsGenerateCallback` | |
| `SetEmbeddingsBatchCallback` | |

### 9. Introspection, warnings, and test hooks

| Member | Notes |
|--------|--------|
| `IsRunning` | |
| `LastWarning` | |
| *(private)* `EnsureFixtureParityValidated` | Parity / fixture validation |

### 10. Static helper

| Member | Notes |
|--------|--------|
| `ListReservedChatSlashCommandNames` | Reserved slash command names for chat |

---

## Where implementations live (translation units)

| Area | Primary files |
|------|----------------|
| Default handler **sequence** (`RegisterDefaultHandlerSequence`, domain phases) | `GatewayHostRegistrationCoordinator.h` / `GatewayHostRegistrationCoordinator.cpp` |
| Shared **catalog** strings: `GatewayEventCatalogNames`, `BuildGatewayDeepSeekConfigJson`, `MaskGatewaySecret` | `GatewayHostCatalogHelpers.h` / `GatewayHostCatalogHelpers.cpp` |
| **Model list JSON** (`NormalizeModelId`, `BuildModelJson`, model id constants) | `GatewayHostModelHelpers.h` / `GatewayHostModelHelpers.cpp` (`blazeclaw::gateway::GatewayModel`) |
| **Epoch ms** + **unsafe agent file path** gate (shared with handlers) | `GatewayHostProtocolHelpers.h` / `GatewayHostProtocolHelpers.cpp` (`GatewayEpochMilliseconds`, `IsUnsafeGatewayAgentFilePath`) |
| Core lifecycle, transport pump, event builders, routing, `RegisterDefaultHandlers` only | `GatewayHost.cpp` |
| Tool execution history handlers (`gateway.tools.executions.*`) | `GatewayHostHandlersToolExecution.h` / `GatewayHost.Handlers.ToolExecutionHistory.cpp` |
| Event catalog query handlers + `gateway.tools.categories` | `GatewayHostHandlersEventCatalogQuery.h` / `GatewayHost.Handlers.EventCatalogQuery.cpp` |
| Registry introspection + agent files + models list + `gateway.tools.call.execute` | `GatewayHostHandlersRegistryIntrospection.h` / `GatewayHost.Handlers.RegistryIntrospection.cpp` |
| Agent/session/channel mutation surface (`gateway.agents.*`, `gateway.sessions.*`, `gateway.features.list`, `gateway.channels.*`) | `GatewayHostHandlersAgentSessionMutation.h` / `GatewayHost.Handlers.AgentSessionMutation.cpp` |
| Agent/tool “surface” (`gateway.agents.get`, tools catalog/preview, `gateway.agents.activate`) | `GatewayHostHandlersAgentToolSurface.h` / `GatewayHost.Handlers.AgentToolSurface.cpp` |
| Config + diagnostics (`gateway.config.*`, logs tail, sessions resolve/create/reset, `gateway.health`) | `GatewayHostHandlersConfigDiagnostics.h` / `GatewayHost.Handlers.ConfigDiagnostics.cpp` |
| Supplementary catalog probes (`gateway.session.list`, `gateway.events.*`, `gateway.tools.*`, `gateway.models.*`, `gateway.health.details`, …) | `GatewayHostHandlersSupplementaryCatalog.h` / `GatewayHost.Handlers.SupplementaryCatalog.cpp` |
| Registry / task-delta JSON serializers (`SerializeSession`, `SerializeTool`, `SerializeTaskDeltaState`, `EscapeJsonString`, …) | `GatewayJsonSerializers.h` / `GatewayJsonSerializers.cpp` |
| Minimal JSON fragment builders (`JsonObject`, `JsonString`, `JsonArray`, …) | `GatewayJsonBuilder.h` / `GatewayJsonBuilder.cpp` |
| Request `params` field access (`RequestParamsView::GetString` / `GetBool` / `GetSize` / `GetObject`) | `GatewayRequestParams.h` / `GatewayRequestParams.cpp` |
| Protocol success-frame helper `protocol::OkResponse` | `GatewayProtocolModels.h` |
| `protocol::EncodeValidatedEvent` (validate + optional schema-error fallback + encode) | `GatewayProtocolCodec.h` / `GatewayProtocolCodec.cpp` |
| Static handler table helper `RegisterStaticPayloadHandlers` | `GatewayStaticRegistration.h` / `GatewayStaticRegistration.cpp` |
| Channel-related handlers | `GatewayHost.Handlers.Channels.cpp` |
| Event catalog / event handlers | `GatewayHost.Handlers.Events.cpp` |
| Tool listing / execution handlers | `GatewayHost.Handlers.Tools.cpp` |
| Scope cluster | `GatewayHost.Handlers.ScopeCluster.cpp` + generated catalog |
| Security ops | `GatewayHost.Handlers.SecurityOps.cpp` |
| Chat/runtime/task-delta-heavy handlers | `GatewayHost.Handlers.Runtime.cpp` |
| Transport method handlers | `GatewayHost.Handlers.Transport.cpp` |
| Staged `chat.send` path | `GatewayHostEx.cpp` / `GatewayHostEx.h` |

---

## Refactoring suggestions (align responsibility with “gateway host”)

These aim to keep **one clear façade** for the shell while shrinking what `GatewayHost` *does* itself.

1. **Clarify the core responsibility**  
   Treat `GatewayHost` as: **wire transport ↔ dispatcher ↔ policy/router ↔ optional stage host**, plus **minimal** shared state. Move domain logic that remains in lambdas toward **named handler classes** (or `Gateway*MethodHandler` types) per area—registration stays thin. **Update:** `RegisterDefaultHandlers` is now a one-line delegate to **`GatewayHostRegistration::RegisterDefaultHandlerSequence`** with **named domain phases** in `GatewayHostRegistrationCoordinator.cpp`. Further extraction of per-method logic can follow the same file boundaries (`GatewayHost.Handlers.*`).

2. **Separate “protocol surface” from “transport surface”**  
   Public methods split roughly into: (a) `IGatewayHostRuntime` + dispatcher-backed protocol, (b) WebSocket/text pump API, (c) event JSON builders, (d) in-process tool execution. Consider a **small public façade** (`GatewayHost`) delegating to internal types, e.g. `GatewayTransportSession` (accept/pump/drain), `GatewayEventFrameCodec` (`Build*` methods), `GatewayRuntimeToolFacade` (list/execute/register), so the class does not advertise every sub-concern in one flat API unless the shell truly needs it.

3. **Keep routing policy in one place**  
   `RouteRequest` already uses `GatewayHostRouter` and `GatewayHostEx`. Document and enforce that **all method-level routing policy** (feature flags, cohorts, fallback) lives in **router + telemetry helpers**, not scattered in handlers. Add tests around `GatewayHostRouteDecision` for new methods if more bypass legacy dispatch.

4. **Health semantics**  
   `IsHealthy()` currently reflects dispatcher initialization. If the product meaning should include transport listening, stage host, or subscriptions, either **compose** those checks or document that health is “dispatch ready” only—avoid misleading UI.

5. **Task deltas**  
   Persistence is associated with `TaskDeltaRepository`; `GatewayHost` **orchestrates** load/save timing. Task-delta **wire JSON** is built via `SerializeTaskDeltaEntry` / `SerializeTaskDeltaState` in `GatewayJsonSerializers.cpp`; normalization/validation stay in `TaskDeltaSchemaValidator` and related units.

6. **Reduce `RegisterDefaultHandlers` surface**  
   **Done (phase 1):** inline registrations were split into `RegisterToolExecutionHistoryHandlers`, `RegisterGatewayEventCatalogQueryHandlers`, `RegisterGatewayRegistryIntrospectionHandlers`, `RegisterGatewayAgentSessionMutationHandlers`, `RegisterGatewayAgentToolSurfaceHandlers`, `RegisterGatewayConfigAndDiagnosticsHandlers`, and `RegisterGatewaySupplementaryCatalogHandlers`; `RegisterDefaultHandlers()` is now a short coordinator. **Done (phase 2):** success-path boilerplate uses `protocol::OkResponse` (see subsection above). **Done (phase 3):** table-driven static registrations via `RegisterStaticPayloadHandlers` and the manifest-driven catalog (see **Table-driven static registrations** above). **Done (phase 4):** `protocol::EncodeValidatedEvent` for event frames (see subsection above). **Done (phase 5):** registry/task-delta serializers live in `GatewayJsonSerializers.*` (see subsection above). **Done (phase 6):** `GatewayJsonBuilder` + `RequestParamsView` (see subsections above; §5 / §7 in the size-reduction section).

7. **Naming consistency**  
   Align “Bootstrap*” vs “CreateRuntimeState” / `InitializeRuntime` naming in docs so phased startup order is obvious to maintainers.

---

## GatewayHost.cpp Size-Reduction Analysis

`GatewayHost.cpp` is **much smaller** than before: default gateway methods live in **`GatewayHost.Handlers.*`** translation units behind **`RegisterAll`** helpers; this file keeps routing, lifecycle, event builders, and staged runtime wiring.

### Main Size Drivers

1. ~~Extremely long `RegisterDefaultHandlers()` method~~ **`RegisterDefaultHandlers()` is a one-line coordinator;** domain registration is split across `GatewayHost.Handlers.*` and generated/manifest outputs. Remaining volume here: **routing** (`RouteRequest` / `GatewayHostRouter`), bootstrap, and **non-dispatcher** helpers.
2. ~~Repeated event-frame construction + schema-validation fallback in `Build*EventFrame` methods.~~ Centralized in `protocol::EncodeValidatedEvent` (see subsection above).
3. Repeated manual JSON string formatting in handlers (`{"x":...}` patterns). Success `ResponseFrame` construction is centralized via `protocol::OkResponse`; JSON *content* is still mostly manual concatenation.
4. Repeated parameter extraction patterns (`ExtractStringParam`, `ExtractBooleanParam`, `ExtractNumericParam`) used the same way across handlers.

### Practical Ways to Reduce File Size

#### 1) Split handler registration by domain (highest impact) — **done for default gateway surface**

`RegisterDefaultHandlers()` delegates to split translation units only (see coordinator). Implemented in `GatewayHost.cpp` **only** as `RegisterDefaultHandlers` → `RegisterDefaultHandlerSequence`.

- **`RegisterToolExecutionHistoryHandlers`** → `GatewayHost.Handlers.ToolExecutionHistory.cpp` (`handlers::tool_execution::ToolExecutionHistoryHandlers`).
- **`RegisterGatewayEventCatalogQueryHandlers`** → `GatewayHost.Handlers.EventCatalogQuery.cpp` (`handlers::event_catalog_query::EventCatalogQueryHandlers`).
- **`RegisterGatewayRegistryIntrospectionHandlers`** → `GatewayHost.Handlers.RegistryIntrospection.cpp` (`handlers::registry_introspection::RegistryIntrospectionHandlers::RegisterAll`).
- **`RegisterGatewayAgentSessionMutationHandlers`** → `GatewayHost.Handlers.AgentSessionMutation.cpp` (`handlers::agent_session_mutation::AgentSessionMutationHandlers::RegisterAll`).
- **`RegisterGatewayAgentToolSurfaceHandlers`** → `GatewayHost.Handlers.AgentToolSurface.cpp` (`handlers::agent_tool_surface::AgentToolSurfaceHandlers::RegisterAll`).
- **`RegisterGatewayConfigAndDiagnosticsHandlers`** → `GatewayHost.Handlers.ConfigDiagnostics.cpp` (`handlers::config_diagnostics::ConfigDiagnosticsHandlers::RegisterAll`).
- **`RegisterGatewaySupplementaryCatalogHandlers`** → `GatewayHost.Handlers.SupplementaryCatalog.cpp` (`handlers::supplementary_catalog::SupplementaryCatalogHandlers::RegisterAll`).

**Further splits (optional):** sub-split very large `RegisterAll` bodies (e.g. agent vs channel) only if a single TU becomes hard to navigate.

**Effect:** shared catalog/deepseek/model/path/time helpers live in **`GatewayHostCatalogHelpers.*`**, **`GatewayHostModelHelpers.*`**, **`GatewayHostProtocolHelpers.*`**; `GatewayHost.cpp` no longer carries the default handler lambda bulk.

#### 2) Move static/seeded handlers to table-driven registration — **in progress / done for main static surfaces**

- **C++ table:** `StaticPayloadHandlerEntry` + `RegisterStaticPayloadHandlers` drives fixed JSON rows (see `GatewayHost.Handlers.Events.cpp` — `kEventsStaticPayloadHandlers`).
- **Manifest + generator:** `GatewayHandlers.manifest.json` (`kind: "static"` / `"toolsMetric"`) continues to drive `GatewayHandlerCatalog.Generated.cpp`.

Handlers that need **captures** (`[this]`, telemetry, registry state) stay as explicit lambdas next to the table call.

**Effect:** large line reduction in `GatewayHost.Handlers.Events.cpp` for the key/scope static grid; other files can adopt the same helper where a method list is purely static JSON.

#### 3) Add reusable response helpers — **`OkResponse` done**

`protocol::OkResponse(request, payloadJson)` lives in `GatewayProtocolModels.h` and replaces the repeated success `protocol::ResponseFrame{ .id, .ok = true, .payloadJson, .error = nullopt }` pattern across gateway handler translation units.

**Done:** `protocol::ErrorResponse(...)` overloads and **`JsonPayloadExists` / `JsonPayloadCount` / `JsonPayloadFoundCount` / `JsonPayloadPathExists`** in `GatewayJsonBuilder.*` for common success shapes. Tests: `BlazeClawMfc/tests/GatewayProtocolResponseHelpersTests.cpp`.

**Effect:** fewer lines per handler return; readability improved. Remaining bulk is still JSON string assembly, not frame wiring.

#### 4) Consolidate event frame build + schema fallback logic — **done**

`protocol::EncodeValidatedEvent(eventName, payloadJson, seq, validationStage)` in `GatewayProtocolCodec` implements validate → fallback → `EncodeEventFrame`. `GatewayHost` `Build*EventFrame` helpers and `GatewayEventFanoutService::BuildChatLifecycleEventFrame` delegate to it.

**Effect:** less duplication; same wire and fallback behavior as before.

#### 5) Extract local JSON builder utilities — **done**

`GatewayJsonBuilder` provides `JsonString`, `JsonBool`, `JsonNumber`, `JsonObject`, `JsonArray` (see subsection **Lightweight JSON builder** above). Ad-hoc handler payloads in `GatewayHost.cpp` still use `EscapeJson` / string concatenation where migration is low value; new structured blobs should prefer `JsonObject` + `JsonString` for consistent escaping.

#### 6) Separate serialization helpers into dedicated file — **done**

Moved to `GatewayJsonSerializers.h` / `GatewayJsonSerializers.cpp`: `EscapeJsonString`, `SerializeSession`, `SerializeAgent`, `SerializeTool`, `SerializeChannel*` (`Status`, `Account`, `Route`), `SerializeAgentFile`, `SerializeAgentFileContent`, `SerializeToolExecution`, `SerializeChannelAdapter`, `SerializeStringArray`, `SerializeTaskDeltaEntry`, `SerializeTaskDeltaState`. `GatewayHost.cpp` retains a thin `EscapeJson` → `EscapeJsonString` shim for inline ad-hoc JSON in handlers.

**Effect:** smaller `GatewayHost.cpp`; serializer behavior and payload shapes unchanged.

#### 7) Centralize request parameter access — **done**

`RequestParamsView` replaces the former `ExtractStringParam` / `ExtractBooleanParam` / `ExtractNumericParam` / `ExtractObjectParam` helpers in `GatewayHost.cpp`. Handlers use `RequestParamsView(request.paramsJson).Get…` or a single `const RequestParamsView params(request.paramsJson);` when reading several fields (see subsection **`RequestParamsView`** above).

### Recommended Execution Order

1. ~~Split `RegisterDefaultHandlers()` by domain.~~ **Done** (see §1 above).
2. ~~Add `OkResponse` helper and replace boilerplate.~~ **Done** — see `protocol::OkResponse` in `GatewayProtocolModels.h` and the subsection `protocol::OkResponse` (success handler boilerplate) above.
3. ~~Introduce table-driven static registrations.~~ **Done** — see **Table-driven static registrations** (`RegisterStaticPayloadHandlers`, manifest + `Generate-GatewayHandlerCatalog.ps1`).
4. ~~Add `EncodeValidatedEvent(...)` helper.~~ **Done** — see **`protocol::EncodeValidatedEvent`** in `GatewayProtocolCodec.h` / `.cpp` and the subsection above.
5. ~~Move serializers to dedicated files.~~ **Done** — see **`GatewayJsonSerializers`** (`GatewayJsonSerializers.h` / `.cpp`) and §6 in the size-reduction section below.
6. ~~Optionally add lightweight JSON builder and params wrapper.~~ **Done** — **`GatewayJsonBuilder`** (`GatewayJsonBuilder.h` / `.cpp`) and **`RequestParamsView`** (`GatewayRequestParams.h` / `.cpp`); see subsections above and §5 / §7 below.

### Notes / Constraints

- This file appears intentionally seed-heavy for protocol coverage; avoid changing external behavior while refactoring.
- Keep method names and payload shapes stable to preserve parity fixtures and schema validation.
- Refactor in small steps with build + protocol tests after each stage.
- `generated/GatewayHandlerCatalog.Generated.cpp` is emitted by `tools/GatewayHandlerCatalogGenerator/Generate-GatewayHandlerCatalog.ps1`, which now generates `protocol::OkResponse(request, std::move(payload))` for static and tools-metric handlers—re-run the script after manifest changes.
- Adding **new fixed-payload** methods: either append a `kind: "static"` block to `src/gateway/GatewayHandlers.manifest.json` and regenerate, **or** append a row to a `StaticPayloadHandlerEntry` array and call `RegisterStaticPayloadHandlers` (as in `GatewayHost.Handlers.Events.cpp`). Do not duplicate the same method name in both paths.
