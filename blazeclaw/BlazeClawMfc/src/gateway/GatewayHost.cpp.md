# GatewayHost — tracking notes

This file tracks analysis of `blazeclaw::gateway::GatewayHost` (`GatewayHost.h` / split `.cpp` sources). The section at the bottom preserves earlier **file size** guidance for `GatewayHost.cpp`.

---

## Intended role (from `IGatewayHostRuntime` and usage)

`GatewayHost` implements `IGatewayHostRuntime` (`RouteRequest`, `IsHealthy`) and acts as a **composition root** for the in-process gateway: `GatewayMethodDispatcher`, `GatewayWebSocketTransport`, agent/channel/session/tool registries, extension lifecycle, chat pipeline orchestration, policy guard, event fanout, task-delta persistence, and routing to a staged runtime (`GatewayHostEx`) for selected methods (e.g. `chat.send`). In practice it also exposes **transport I/O**, **event frame builders**, **runtime tool registration**, and many **setter** entry points used by the desktop shell.

### `RegisterDefaultHandlers()` call order (maintain when adding methods)

Invoked in this sequence (see `GatewayHost.cpp`):

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

Duplicate method names across registrars still overwrite the same dispatcher slot—keep names unique or rely on last registration intentionally.

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
| *(private)* `RegisterToolExecutionHistoryHandlers` | `gateway.tools.executions.*` (list, count, latest, clear) — `GatewayHost.cpp` |
| *(private)* `RegisterGatewayEventCatalogQueryHandlers` | Event catalog queries + `gateway.tools.categories` — `GatewayHost.cpp` |
| *(private)* `RegisterGatewayRegistryIntrospectionHandlers` | Agents/sessions/tools/models/config/transport “probe” handlers, agent files, models list, `gateway.tools.call.execute` — `GatewayHost.cpp` |
| *(private)* `RegisterGatewayAgentSessionMutationHandlers` | Agent CRUD/run/wait, session lifecycle, `gateway.features.list`, channel status/route — `GatewayHost.cpp` |
| *(private)* `RegisterGatewayAgentToolSurfaceHandlers` | `gateway.agents.get`, `gateway.tools.catalog` / `call.preview`, `gateway.agents.activate` — `GatewayHost.cpp` |
| *(private)* `RegisterGatewayConfigAndDiagnosticsHandlers` | `gateway.config.get` / `set`, `gateway.logs.tail`, session resolve/create/reset, `gateway.health` — `GatewayHost.cpp` |
| *(private)* `RegisterGatewaySupplementaryCatalogHandlers` | Remaining catalog-style methods (`gateway.session.list`, `gateway.events.*`, `gateway.tools.*`, `gateway.models.*`, `gateway.config.*`, logs, health details) — `GatewayHost.cpp` |
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
| Core lifecycle, transport pump, event builders, routing, `RegisterDefaultHandlers` + domain `RegisterGateway*` / `RegisterToolExecution*` helpers | `GatewayHost.cpp` |
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
   Treat `GatewayHost` as: **wire transport ↔ dispatcher ↔ policy/router ↔ optional stage host**, plus **minimal** shared state. Move domain logic that remains in lambdas toward **named handler classes** (or `Gateway*MethodHandler` types) per area—registration stays thin. This matches the existing split files but can go further than “many lambdas in member functions.”

2. **Separate “protocol surface” from “transport surface”**  
   Public methods split roughly into: (a) `IGatewayHostRuntime` + dispatcher-backed protocol, (b) WebSocket/text pump API, (c) event JSON builders, (d) in-process tool execution. Consider a **small public façade** (`GatewayHost`) delegating to internal types, e.g. `GatewayTransportSession` (accept/pump/drain), `GatewayEventFrameCodec` (`Build*` methods), `GatewayRuntimeToolFacade` (list/execute/register), so the class does not advertise every sub-concern in one flat API unless the shell truly needs it.

3. **Keep routing policy in one place**  
   `RouteRequest` already uses `GatewayHostRouter` and `GatewayHostEx`. Document and enforce that **all method-level routing policy** (feature flags, cohorts, fallback) lives in **router + telemetry helpers**, not scattered in handlers. Add tests around `GatewayHostRouteDecision` for new methods if more bypass legacy dispatch.

4. **Health semantics**  
   `IsHealthy()` currently reflects dispatcher initialization. If the product meaning should include transport listening, stage host, or subscriptions, either **compose** those checks or document that health is “dispatch ready” only—avoid misleading UI.

5. **Task deltas**  
   Persistence is already associated with `TaskDeltaRepository`; ensure `GatewayHost` only **orchestrates load/save timing** and does not grow more serialization logic—keep normalization/validation in dedicated units (`TaskDeltaSchemaValidator`, etc.).

6. **Reduce `RegisterDefaultHandlers` surface**  
   **Done (phase 1):** inline registrations were split into `RegisterToolExecutionHistoryHandlers`, `RegisterGatewayEventCatalogQueryHandlers`, `RegisterGatewayRegistryIntrospectionHandlers`, `RegisterGatewayAgentSessionMutationHandlers`, `RegisterGatewayAgentToolSurfaceHandlers`, `RegisterGatewayConfigAndDiagnosticsHandlers`, and `RegisterGatewaySupplementaryCatalogHandlers`; `RegisterDefaultHandlers()` is now a short coordinator. Further work: table-driven static handlers and `OkResponse` helpers (see size-reduction section).

7. **Naming consistency**  
   Align “Bootstrap*” vs “CreateRuntimeState” / `InitializeRuntime` naming in docs so phased startup order is obvious to maintainers.

---

## GatewayHost.cpp Size-Reduction Analysis

`GatewayHost.cpp` remains large because it still holds many `m_dispatcher.Register(...)` lambdas (now grouped under domain `RegisterGateway*` helpers) and repeated JSON/string assembly patterns.

### Main Size Drivers

1. ~~Extremely long `RegisterDefaultHandlers()` method~~ **`RegisterDefaultHandlers()` is now a coordinator;** bulk lives in domain register helpers. Remaining volume: near-identical `m_dispatcher.Register(...)` blocks and JSON concatenation.
2. Repeated event-frame construction + schema-validation fallback in `Build*EventFrame` methods.
3. Repeated manual JSON string formatting in handlers (`{"x":...}` patterns).
4. Repeated parameter extraction patterns (`ExtractStringParam`, `ExtractBooleanParam`, `ExtractNumericParam`) used the same way across handlers.

### Practical Ways to Reduce File Size

#### 1) Split handler registration by domain (highest impact) — **partially done**

`RegisterDefaultHandlers()` delegates to existing split translation units **and** to new helpers in `GatewayHost.cpp`:

- `RegisterToolExecutionHistoryHandlers`
- `RegisterGatewayEventCatalogQueryHandlers`
- `RegisterGatewayRegistryIntrospectionHandlers`
- `RegisterGatewayAgentSessionMutationHandlers`
- `RegisterGatewayAgentToolSurfaceHandlers`
- `RegisterGatewayConfigAndDiagnosticsHandlers`
- `RegisterGatewaySupplementaryCatalogHandlers`

Further splits (optional): move some of these helpers into new `.cpp` files (e.g. `GatewayHost.Handlers.Default.cpp`) or align names with `RegisterChannelsHandlers`-style files; or extract **agent** vs **session** registrars from `RegisterGatewayRegistryIntrospectionHandlers` / `RegisterGatewayAgentSessionMutationHandlers` if those functions grow again.

**Effect so far:** readability gain; total line count in the project is essentially unchanged (code moved, not deleted).

#### 2) Move static/seeded handlers to table-driven registration
Many handlers return fixed payloads or tiny parameterized payloads. Use a table structure:

- method name
- static payload
- optional lambda for dynamic fields

This can replace dozens of repetitive blocks.

**Expected effect:** high line reduction in registration code.

#### 3) Add reusable response helpers
Introduce helpers such as:

- `OkResponse(request, payloadJson)`
- `MakeExistsResponse(...)`
- `MakeCountResponse(...)`

Most handlers repeat the same `protocol::ResponseFrame` boilerplate.

**Expected effect:** medium-to-high reduction.

#### 4) Consolidate event frame build + schema fallback logic
Current `BuildTickEventFrame`, `BuildHealthEventFrame`, `BuildShutdownEventFrame`, etc. repeat this pattern:

- build frame
- validate
- fallback to `gateway.schema.error`
- encode

Create one helper like `EncodeValidatedEvent(eventName, payloadJson, seq, stage)`.

**Expected effect:** medium reduction and less duplication risk.

#### 5) Extract local JSON builder utilities
Current code manually concatenates JSON in many places. Even with string-based JSON (no dependency), small helper functions can reduce repetition:

- `JsonObject({{"key", value}, ...})`
- `JsonBool`, `JsonNumber`, `JsonString`
- `JsonArray(items)`

This can compress repeated formatting and reduce escaping mistakes.

**Expected effect:** medium reduction and safer payload construction.

#### 6) Separate serialization helpers into dedicated file
Move these functions out of `GatewayHost.cpp`:

- `SerializeSession`
- `SerializeAgent`
- `SerializeTool`
- `SerializeChannel*`
- `SerializeAgentFile*`

Suggested files:

- `GatewayJsonSerializers.h`
- `GatewayJsonSerializers.cpp`

**Expected effect:** medium file-size reduction for `GatewayHost.cpp`.

#### 7) Centralize request parameter access
Create a small request-params helper wrapper (still string-based if needed) so handlers call:

- `params.GetString("channel")`
- `params.GetBool("active")`
- `params.GetSize("limit")`

This trims repeated extraction boilerplate inside handlers.

**Expected effect:** low-to-medium reduction.

### Recommended Execution Order

1. ~~Split `RegisterDefaultHandlers()` by domain.~~ **Done** (see §1 above).
2. Add `OkResponse` helper and replace boilerplate.
3. Introduce table-driven static registrations.
4. Add `EncodeValidatedEvent(...)` helper.
5. Move serializers to dedicated files.
6. Optionally add lightweight JSON builder and params wrapper.

### Notes / Constraints

- This file appears intentionally seed-heavy for protocol coverage; avoid changing external behavior while refactoring.
- Keep method names and payload shapes stable to preserve parity fixtures and schema validation.
- Refactor in small steps with build + protocol tests after each stage.
