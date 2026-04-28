---
name: baidu-search
description: Search the web using Baidu AI Search. Use for live information, documentation, and research topics.
command-dispatch: tool
command-tool: baidu-search.search.web
command-arg-mode: raw
command-arg-schema: schema://baidu-search.search.web.args.v1
command-result-schema: schema://baidu-search.search.web.result.v1
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

- `baidu-search.search.web`

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
- `sessionKey` (optional): session-scoped key used by local 429 cooldown memoization
- `simulate429` (optional, test): deterministic synthetic 429 path for smoke/validation

## Retry and Rate-Limit Resilience

The script now includes bounded retry/backoff for `429` and `5xx` responses:

- Exponential backoff + jitter
- Honors `Retry-After` header when present
- Emits structured retry-exhausted error payload fields (attempt counts, cumulative wait, retry-after honored)
- Adds lightweight session+query fingerprint cooldown memo to avoid immediate re-hit storms

### Environment Controls

- `BAIDU_SEARCH_RETRY_MAX_ATTEMPTS` (default `3`)
- `BAIDU_SEARCH_RETRY_BASE_DELAY_MS` (default `800`)
- `BAIDU_SEARCH_RETRY_MAX_DELAY_MS` (default `8000`)
- `BAIDU_SEARCH_RETRY_JITTER_MS` (default `200`)
- `BAIDU_SEARCH_RATE_LIMIT_COOLDOWN_SECONDS` (default `30`)
