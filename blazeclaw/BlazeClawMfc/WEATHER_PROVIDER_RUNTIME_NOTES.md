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
