## GatewayHost.cpp Size-Reduction Analysis

`GatewayHost.cpp` is very large mostly because `RegisterDefaultHandlers()` contains many repetitive handler registrations and repeated JSON/string assembly patterns.

## Main Size Drivers

1. Extremely long `RegisterDefaultHandlers()` method with many near-identical `m_dispatcher.Register(...)` blocks.
2. Repeated event-frame construction + schema-validation fallback in `Build*EventFrame` methods.
3. Repeated manual JSON string formatting in handlers (`{"x":...}` patterns).
4. Repeated parameter extraction patterns (`ExtractStringParam`, `ExtractBooleanParam`, `ExtractNumericParam`) used the same way across handlers.

## Practical Ways to Reduce File Size

### 1) Split handler registration by domain (highest impact)
Move registrations into focused private methods (same class):

- `RegisterEventHandlers()`
- `RegisterToolHandlers()`
- `RegisterChannelHandlers()`
- `RegisterAgentHandlers()`
- `RegisterSessionHandlers()`
- `RegisterConfigHandlers()`
- `RegisterTransportHandlers()`

Then keep `RegisterDefaultHandlers()` as a short coordinator.

**Expected effect:** major readability gain and large line count reduction per file (total project lines unchanged unless further dedup is applied).

### 2) Move static/seeded handlers to table-driven registration
Many handlers return fixed payloads or tiny parameterized payloads. Use a table structure:

- method name
- static payload
- optional lambda for dynamic fields

This can replace dozens of repetitive blocks.

**Expected effect:** high line reduction in registration code.

### 3) Add reusable response helpers
Introduce helpers such as:

- `OkResponse(request, payloadJson)`
- `MakeExistsResponse(...)`
- `MakeCountResponse(...)`

Most handlers repeat the same `protocol::ResponseFrame` boilerplate.

**Expected effect:** medium-to-high reduction.

### 4) Consolidate event frame build + schema fallback logic
Current `BuildTickEventFrame`, `BuildHealthEventFrame`, `BuildShutdownEventFrame`, etc. repeat this pattern:

- build frame
- validate
- fallback to `gateway.schema.error`
- encode

Create one helper like `EncodeValidatedEvent(eventName, payloadJson, seq, stage)`.

**Expected effect:** medium reduction and less duplication risk.

### 5) Extract local JSON builder utilities
Current code manually concatenates JSON in many places. Even with string-based JSON (no dependency), small helper functions can reduce repetition:

- `JsonObject({{"key", value}, ...})`
- `JsonBool`, `JsonNumber`, `JsonString`
- `JsonArray(items)`

This can compress repeated formatting and reduce escaping mistakes.

**Expected effect:** medium reduction and safer payload construction.

### 6) Separate serialization helpers into dedicated file
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

### 7) Centralize request parameter access
Create a small request-params helper wrapper (still string-based if needed) so handlers call:

- `params.GetString("channel")`
- `params.GetBool("active")`
- `params.GetSize("limit")`

This trims repeated extraction boilerplate inside handlers.

**Expected effect:** low-to-medium reduction.

## Recommended Execution Order

1. Split `RegisterDefaultHandlers()` by domain.
2. Add `OkResponse` helper and replace boilerplate.
3. Introduce table-driven static registrations.
4. Add `EncodeValidatedEvent(...)` helper.
5. Move serializers to dedicated files.
6. Optionally add lightweight JSON builder and params wrapper.

## Notes / Constraints

- This file appears intentionally seed-heavy for protocol coverage; avoid changing external behavior while refactoring.
- Keep method names and payload shapes stable to preserve parity fixtures and schema validation.
- Refactor in small steps with build + protocol tests after each stage.
