---
name: baidu-search
description: Search the web using Baidu AI Search. Use for live information, documentation, and research topics.
command-dispatch: tool
command-tool: baidu_search.search.web
command-arg-mode: raw
command-arg-schema: schema://baidu_search.search.web.args.v1
command-result-schema: schema://baidu_search.search.web.result.v1
command-idempotency-hint: safe
command-retry-policy-hint: transient-network
command-requires-approval: false
metadata:
  blazeclaw:
    emoji: "🔍"
    requires:
      bins:
        - python
      env:
        - BAIDU_API_KEY
---

# Baidu Search

Search the web via Baidu AI Search API.

## Prerequisites

- `BAIDU_API_KEY` must be set.
- Python runtime must be available.

See: `references/apikey-fetch.md`

## Tool Surface

- `baidu_search.search.web`

See:

- `tool-manifest.json`
- `tool-contracts.json`

## Script Usage

```bash
python scripts/search.py '{"query":"人工智能"}'
```

## Arguments

- `query` (required): search query text
- `count` (optional): 1-50, default 10
- `freshness` (optional): `pd`/`pw`/`pm`/`py` or `YYYY-MM-DDtoYYYY-MM-DD`
