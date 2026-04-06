# Brave Search Tool Surface

This document describes the BlazeClaw tool-facing surface for
`blazeclaw/skills/brave-search`.

## Namespace

- `brave_search`

## Tools

### 1) `brave_search.search.web`

Search the web and return Brave search results, optionally including
readable page content.

**Runtime mapping:**
- Entry: `scripts/search.js`
- Runtime type: `node-cli`

**Input contract:**
- `query` (string, required)
- `count` (integer, optional, 1..20)
- `content` (boolean, optional)

**Example args:**

```json
{
  "query": "MFC CListBox horizontal extent",
  "count": 5,
  "content": false
}
```

### 2) `brave_search.fetch.content`

Fetch a single URL and extract readable markdown content.

**Runtime mapping:**
- Entry: `scripts/content.js`
- Runtime type: `node-cli`

**Input contract:**
- `url` (string, required)

**Example args:**

```json
{
  "url": "https://example.com/article"
}
```

## Notes

- Tool IDs and schema references are defined in `tool-manifest.json`.
- JSON contracts are defined in `tool-contracts.json`.
- Runtime startup now loads local `tool-manifest.json` files into
  `gateway.tools.list` / `gateway.tools.catalog`.

## Runtime discovery checks

Use the gateway endpoints below to verify Phase 3 discovery:

- `gateway.tools.list` (expect `brave_search.search.web` and
  `brave_search.fetch.content`)
- `gateway.tools.catalog` (same tool IDs present in full catalog)

## Current execution status

- Runtime discovery is implemented.
- Runtime execution wiring is implemented for:
  - `brave_search.search.web`
  - `brave_search.fetch.content`

## Chat orchestration status

- `SKILL.md` frontmatter now exposes command dispatch metadata pointing to
  `brave_search.search.web`.
- Embedded orchestration can resolve brave-search command bindings and execute
  runtime tool calls through gateway V2 execution path.

## Phase 5 hardening

- Input guardrails:
  - `query` required, trimmed, control-char rejected, max length enforced
  - `count/topK` integer validation with bounded range
  - `url` required, trimmed, control-char rejected, HTTP/HTTPS enforced
- Runtime controls:
  - bounded execution timeout per tool operation
  - output truncation policy to protect UI/runtime channels
  - deterministic failure code mapping (`auth_error`, `rate_limited`,
    `upstream_unavailable`, `network_error`, `process_exit_nonzero`)
- Optional credential policy:
  - `BLAZECLAW_BRAVE_REQUIRE_API_KEY=true` enforces preflight on
    `BRAVE_API_KEY` availability.
