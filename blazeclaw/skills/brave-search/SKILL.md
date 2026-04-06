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

Optional runtime policy env:

- `BLAZECLAW_BRAVE_REQUIRE_API_KEY=true` to enforce API key preflight.

## Chat Usage Examples

- "Search for latest C++23 coroutine best practices and summarize key changes."
- "Find official Microsoft docs about MFC dockable panes and extract the most relevant page content."
- "Look up BlazeClaw gateway tool registry behavior and provide a short digest."

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
- Runtime hardening and configuration controls are implemented (Phase 5).

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

## Troubleshooting

- `brave_api_key_missing`
  - Set `BRAVE_API_KEY` or disable strict preflight by leaving
    `BLAZECLAW_BRAVE_REQUIRE_API_KEY` unset/false.
- `invalid_arguments`
  - Ensure `query` is non-empty for search and `url` is a valid
    `http://` or `https://` value for content fetch.
- `timed_out`
  - Retry with a smaller result count or without `--content`.
- `network_error` / `upstream_unavailable`
  - Check outbound network/proxy settings and retry.

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
