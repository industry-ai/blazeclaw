# PORTING_PLAN: humanizer

## Upstream

- Align with OpenClaw skill intent: natural-language rewrite of AI-sounding text (`openclaw/skills/` if a matching package exists).

## BlazeClaw differences

- Local adapter `humanizer.rewrite` for ordered content-polishing chains; `_meta.json` and `.clawhub/origin.json` for catalog metadata.

## Validation

- Runtime catalog lists tool; exercise via chat orchestration with `summarize` / email flows per `skills/readme.md`.
