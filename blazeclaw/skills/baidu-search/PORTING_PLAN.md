# PORTING_PLAN: baidu-search

## Upstream

- Reference: `openclaw/skills/` (search for `baidu` or equivalent upstream skill if present); BlazeClaw implementation is workspace-local.

## BlazeClaw differences

- Python-based search under `scripts/`; WebView2 `config.html` with `~/.config/baidu-search/.env` persistence (see `blazeclaw/skills/readme.md`).
- Windows-focused; API keys via env / config bridge.

## Validation

- Manual: configure API key, invoke search tool from chat/runtime catalog.
- Expand this section when parity fixtures exist.
