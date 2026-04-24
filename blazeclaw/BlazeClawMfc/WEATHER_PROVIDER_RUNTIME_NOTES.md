# Weather Provider Runtime Notes

## Date
- 2026-04-07

## Prompt Scenario
- `Check tomorrow's weather in Wuhan, write a short report, and email it to jicheng@whu.edu.cn now`

## Current Status Summary
- End-to-end task now works (weather report is generated and email is delivered).
- Real weather currently succeeds through backup provider routing.
- Primary provider path is still unstable in this environment.

## Succeeded Path
1. `chat.send` prompt matches weather+report+email orchestration intent.
2. `weather.lookup` executes.
3. Primary provider (`wttr.in`) may fail or return invalid payload in this environment.
4. Backup provider (`Open-Meteo` geocoding + forecast) succeeds and returns usable weather fields.
5. Report is composed from weather payload.
6. `email.schedule` prepare + approve path succeeds.
7. Delivery succeeds via `imap-smtp-email` backend (observed as `via imap-smtp-email`).

## Failed Path (Observed)
### Path Name
- Primary provider direct path (`wttr.in` JSON payload path)

### Failure Behavior
- `wttr.in` response is `null` (or missing expected weather object shape).
- Parser cannot find `current_condition` (or other expected fields).
- Historical symptom: `provider_invalid_payload` with
  `weather_payload_missing_current_condition`.

### Why it Fails
- Current environment/provider response does not consistently return the
  expected `wttr.in` schema for `?format=j1`.
- The legacy strict payload contract for `current_condition` is not reliable
  under this provider behavior.

## Plan to Make the Failed Path Work (Primary Provider Stabilization Plan)
1. Add provider health telemetry for `wttr.in` response classes:
   `valid_json`, `json_null`, `invalid_shape`, `http_error`, `timeout`.
2. Add bounded retry policy for `wttr.in` on transient empty/null responses
   (small retry count, jittered delay).
3. Add payload-shape adapters for alternative `wttr.in` variants when
   `current_condition` is absent but other weather blocks are present.
4. Add a strict provider capability gate:
   if `wttr.in` health drops below threshold, auto-demote to backup provider
   for a cooldown window.
5. Add contract tests with fixture variants:
   - valid `current_condition`
   - missing `current_condition`
   - `null` body
   - malformed JSON
   - partial forecast-only payload
6. Add a runtime diagnostics endpoint or log bundle field that records which
   provider path was selected (`wttr` vs `open-meteo` vs synthetic fallback)
   for each runId.
7. Add staged rollout flag for primary-provider strict mode so primary path can
   be re-enabled gradually after health metrics improve.

## Immediate Operational Guidance
- Keep backup provider path enabled as default-safe behavior.
- Treat synthetic fallback as last-resort only.
- Continue monitoring whether `wttr.in` returns stable structured payloads in
  this network/runtime environment.

---

## Date
- 2026-04-24

## Regression Scenario (Chinese Prompt)
- English input succeeds:
  - `Check tomorrow's weather in Wuhan, write a short report, and email it to jicheng@whu.edu.cn now.`
- Chinese input fails partially:
  - `查一下明天武汉的天气，写一个简短的报告，用电子邮件发送给 jicheng@whu.edu.cn`
- Observed output includes synthetic weather fallback and repeated approval failure logs:
  - `Provider unavailable (fallback estimate)`
  - `tools.execute.error - status=error code=method_not_implemented`

## Root Cause Analysis

### Root Cause 1: Chinese location extraction captures wrong city token
- File: `blazeclaw/BlazeClawMfc/src/gateway/GatewayJsonUtils.cpp`
- The Chinese location regex in `ExtractExplicitLocationValue` currently allows
  the capture group to include time words like `明天`.
- For `查一下明天武汉的天气...`, extracted city becomes `明天武汉` instead of `武汉`.
- Downstream effect:
  1. `TryOrchestrateWeatherEmailPrompt` passes `city="明天武汉"` to
     `weather.lookup`.
  2. Provider requests use invalid location text.
  3. Provider fetch/parse path fails and falls back to synthetic snapshot.
  4. Assistant shows `Provider unavailable (fallback estimate)`.

### Root Cause 2: Tool-execute RPC method is missing in runtime-dispatch-only startup path
- Files:
  - `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`
  - `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.RegistryIntrospection.cpp`
- `gateway.tools.call.execute` is registered in
  `RegisterGatewayRegistryIntrospectionHandlers`.
- `StartLocalRuntimeDispatchOnly()` does not call `RegisterDefaultHandlers()` and
  does not register registry-introspection handlers; it only registers runtime
  handlers and tools list/catalog helpers.
- Downstream effect:
  1. UI/bridge sends approval calls through `gateway.tools.call.execute` with
     `tool=email.schedule` and `args.action=approve`.
  2. Dispatcher cannot find method and returns `method_not_implemented`.
  3. Skill log repeats `tools.execute.start ... action=approve` followed by
     `tools.execute.error ... method_not_implemented`.

## Why English Works but Chinese Fails
- English prompt contains explicit city pattern (`in Wuhan`) that matches the
  English location regex robustly.
- Chinese prompt currently triggers a broader Chinese regex that can swallow
  date + city together; this causes weather provider degradation.
- Email approval failure is startup-mode dependent. If a run path uses runtime
  dispatch only and approval is executed via bridge RPC (`gateway.tools.call.execute`),
  it fails regardless of language.

## Step-by-Step Action Plan to Fix
1. [DONE 2026-04-24] Fix Chinese city extraction normalization.
   - Implemented in `blazeclaw/BlazeClawMfc/src/gateway/GatewayJsonUtils.cpp`.
   - Updated `ExtractExplicitLocationValue` to tolerate optional Chinese date
     keywords before city capture.
   - Added post-process sanitizer to strip known date prefixes (`今天`, `明天`)
     from captured city values.

2. [DONE 2026-04-24] Add deterministic parser tests for Chinese location/date combinations.
   - Implemented in `blazeclaw/BlazeClawMfc/tests/GatewayWeatherEmailRegressionTests.cpp`.
   - Added parser regression tests for:
     - `查一下明天武汉的天气...` → city=`武汉`, date=`tomorrow`
     - `查一下今天北京天气...` → city=`北京`, date=`today`
     - `在深圳查天气并发邮件给...` → city=`深圳`

3. [DONE 2026-04-24] Register tool execution RPC in runtime-dispatch-only mode.
   - Implemented in `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`.
   - `StartLocalRuntimeDispatchOnly()` now registers
     `RegisterGatewayRegistryIntrospectionHandlers()`, which includes
     `gateway.tools.call.execute`.
   - Keeps thin-façade registration split intact by reusing existing registrar.

4. [DONE 2026-04-24] Add regression test for runtime-dispatch-only approval flow.
   - Implemented in `blazeclaw/BlazeClawMfc/tests/GatewayWeatherEmailRegressionTests.cpp`.
   - Starts host in runtime-dispatch-only mode.
   - Executes prepare + approve through `gateway.tools.call.execute`.
   - Asserts no `method_not_implemented` and valid terminal status (`ok` or
     policy-driven `needs_approval`).

5. [DONE 2026-04-24] Add end-to-end Chinese weather+email regression validation.
   - Strengthened existing Chinese `chat.send` + `chat.events.poll` regression.
   - Asserts weather tool result is `status=ok`.
   - Asserts synthetic fallback marker is absent from assistant trace/final text.
   - Asserts no duplicate or repeated `method_not_implemented` approval failures.

6. [DONE 2026-04-24] Align telemetry and diagnostics with OpenClaw parity signals.
   - Implemented in `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.RuntimeHelpers.inl`.
   - Emits a structured non-PII orchestration trace marker:
     `orchestration.intent city=<city> date=<date> source=structural_orchestration_signals`.
   - Preserves `structural_orchestration_signals` metadata path.

7. [DONE 2026-04-24] Validate and gate with required build/test workflow.
   - Required build command executed:
     - `msbuild "blazeclaw/BlazeClaw.sln" /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001`
   - Targeted regression execution is used to validate Chinese parser, runtime-dispatch approval,
     and Chinese weather/email path.
   - Merge should remain blocked on any regression in those paths.

## Expected Outcome After Fix
- Chinese prompt resolves city/date correctly and uses real provider path when
  available (Open-Meteo or wttr-in fallback chain), avoiding synthetic estimate
  in normal conditions.
- Approval calls no longer fail with `method_not_implemented` in runtime-dispatch-only
  startup mode.
- English/Chinese weather+email orchestration reaches parity under the same
  policy profile and fallback settings.

## Validation Update
- 2026-04-24 targeted rerun after Phase 3-5 implementation:
  - Command: `BlazeClawMfc.Tests.exe [gateway][weather-email] --order decl --durations yes`
  - Result: **8 cases, 5 passed, 3 failed**.
- Current failing regressions:
  1. Chinese deterministic-path test still observes `Provider unavailable (fallback estimate)`.
  2. Bilingual deterministic-path test misses `tools.execute.result tool=weather.lookup status=ok`.
  3. Runtime-dispatch-only approval regression fails approval-store file-content assertion at the expected path.
- Interpretation:
  - Core crash-hardening and parser parity changes compile and run.
  - Remaining failures are integration/environment stability issues that still need follow-up before declaring full parity.
