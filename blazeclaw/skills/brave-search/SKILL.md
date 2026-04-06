---
name: brave-search
description: Web search and content extraction via Brave Search API. Use for searching documentation, facts, or any web content. Lightweight, no browser required.
command-dispatch: tool
command-tool: brave_search.search.web
command-arg-mode: raw
command-arg-schema: schema://brave_search.search.web.args.v1
command-result-schema: schema://brave_search.search.web.result.v1
command-idempotency-hint: safe
command-retry-policy-hint: transient-network
command-requires-approval: false
---

# Brave Search

Headless web search and content extraction using Brave Search. No browser required.

## Setup

Run once before first use:

```bash
cd blazeclaw/skills/brave-search
npm ci
```

Needs env: `BRAVE_API_KEY`.

## Tool Surface

This skill now exposes BlazeClaw tool contracts:

- `brave_search.search.web`
- `brave_search.fetch.content`

See:

- `tool-manifest.json`
- `tool-contracts.json`
- `TOOL_SURFACE.md`

## Integration Status

- Discovery in runtime catalogs is available.
- Chat-callable runtime execution path is implemented (Phase 4).
- Remaining hardening and configuration closure is tracked under Phase 5.

## Search

```bash
./scripts/search.js "query"                    # Basic search (5 results)
./scripts/search.js "query" -n 10              # More results
./scripts/search.js "query" --content          # Include page content as markdown
./scripts/search.js "query" -n 3 --content     # Combined
```

## Extract Page Content

```bash
./scripts/content.js https://example.com/article
```

Fetches a URL and extracts readable content as markdown.

## Output Format

```
--- Result 1 ---
Title: Page Title
Link: https://example.com/page
Snippet: Description from search results
Content: (if --content flag used)
  Markdown content extracted from the page...

--- Result 2 ---
...
```

## When to Use

- Searching for documentation or API references
- Looking up facts or current information
- Fetching content from specific URLs
- Any task requiring web search without interactive browsing
