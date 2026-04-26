---
name: web-browsing
description: Browse and summarize websites, extract content from URLs, and search the web for current information.
command-dispatch: tool
command-tool: web_browsing.search.web
command-arg-mode: raw
command-arg-schema: schema://web_browsing.search.web.args.v1
command-result-schema: schema://web_browsing.search.web.result.v1
command-idempotency-hint: safe
command-retry-policy-hint: transient-network
command-requires-approval: false
metadata:
  blazeclaw:
    emoji: "🌐"
    requires:
      bins:
        - node
---

# Web Browsing

Web search and readable page-content extraction for ordered workflows.

## Tool Surface

- `web_browsing.search.web`
- `web_browsing.fetch.content`

Runtime implementation files:

- `tool-manifest.json`
- `tool-contracts.json`
- `scripts/search_web.py`
- `scripts/fetch_content.py`

## Argument Notes

- `web_browsing.search.web` requires `query` and accepts optional `count`.
- `web_browsing.fetch.content` requires `url` with `http://` or `https://`.

If arguments are missing or invalid, runtime returns explicit argument errors.

## Orchestration Compatibility

- Ordered prompts may reference either `web-browsing` or `web_browsing`.
- Runtime sequencing resolver normalizes both forms to deterministic tool targets.

## Configuration UI

- `config.html` provides bridge-based save/load for skill defaults.
