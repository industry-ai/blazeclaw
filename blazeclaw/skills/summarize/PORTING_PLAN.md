# PORTING_PLAN: summarize

## Upstream

- Conceptual match: OpenClaw summarization skills under `openclaw/skills/` (path may differ).

## BlazeClaw differences

- Local adapter `summarize.extract`; `blazeclaw.conf` sample; integrated in deterministic polishing chains with `humanizer` / `imap-smtp-email`.
- Phase 1 input-selection hardening implemented in runtime adapter path:
  - quote-aware source extraction (`"..."`, `'...'`, `“...”`, `‘...’`, `「...」`, `『...』`),
  - candidate ranking with control-phrase de-prioritization,
  - control-only/low-quality fragment rejection with deterministic `invalid_arguments` feedback.
- Phase 2 multilingual structured extraction implemented in `BuildSummarizeExtractOutput`:
  - Time: English (`next ... at ...`) + Chinese weekday/relative-day/clock patterns.
  - Location: English meeting-room pattern + Chinese `在...会议室/会议厅` pattern.
  - People: bilingual detection for `boss/we/team` and `老板/领导/我们/团队`.
- Phase 5 rollout controls + diagnostics implemented:
  - feature flag `BLAZECLAW_SUMMARIZE_MULTILINGUAL_EXTRACTOR_ENABLED` (default `true`),
  - diagnostics flag `BLAZECLAW_SUMMARIZE_EXTRACTION_DIAGNOSTICS_ENABLED` (default `false`),
  - lightweight diagnostics emitted without raw-content leakage:
    - draft candidate count,
    - selected candidate length,
    - extraction confidence bucket (`high|medium|low`).
- 2026-04 Chinese-vs-English parity hardening implemented across gateway + extraction path:
  - `gateway.tools.call.execute` args normalization now accepts alias containers (`args`, `arguments`, `parameters`, `tool_arguments`, `toolArguments`, `payload`) including JSON-string encoded payloads.
  - request schema validation for `gateway.tools.call.execute` now mirrors alias acceptance (`object|string|array`) to prevent language-dependent payload drops.
  - Chinese workflow draft marker extraction expanded to tolerate full-width punctuation and quote variants, with workflow-aware quoted fallback for mixed orchestration prompts.

## Validation

- Chat-driven ordered flows; verify tool ID resolution in runtime catalog.
- Added extraction regression coverage in `BlazeClawMfc/tests/ToolRuntimeRegistryIntegrationTests.cpp` for Chinese wrapped prompts and control-only fragment rejection.
- Added multilingual extraction regression coverage for pure Chinese and mixed-language draft text.
- Added feature-flag regression coverage to verify multilingual extractor can be toggled off deterministically.
- Added execute-args alias regression coverage for `gateway.tools.call.execute` (including alias priority and JSON-string payload decode behavior).
- Added bilingual EN/ZH parity regression and full-width punctuation/quote variant extraction coverage for ordered workflow prompts.
