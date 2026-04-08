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

**Chat-oriented example args:**

```json
{
  "query": "BlazeClaw gateway.tools.list behavior",
  "count": 3,
  "content": true
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

**Chat-oriented example args:**

```json
{
  "url": "https://learn.microsoft.com/cpp/mfc/reference/cdockablepane-class"
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

- `SKILL.md` command-dispatch metadata is intentionally disabled.
- This prevents brave-search from auto-capturing generic ordered workflows
  that target non-search skills.
- Embedded orchestration still executes brave-search through explicit tool IDs:
  - `brave_search.search.web`
  - `brave_search.fetch.content`

## Phase 5 hardening

- Input guardrails:
  - `query` required, trimmed, control-char rejected, max length enforced
  - `count/topK` integer validation with bounded range
  - `url` required, trimmed, control-char rejected, HTTP/HTTPS enforced
- Runtime controls:
  - bounded execution timeout per tool operation
  - output truncation policy to protect UI/runtime channels
  - compact-query derivation for oversized/noisy search prompts
  - explicit planner error when no safe search query can be derived
  - deterministic failure code mapping (`auth_error`, `rate_limited`,
    `upstream_unavailable`, `network_error`, `process_exit_nonzero`)
- Optional credential policy:
  - `BLAZECLAW_BRAVE_REQUIRE_API_KEY=true` enforces preflight on
    `BRAVE_API_KEY` availability.

## Troubleshooting quick map

- `brave_api_key_missing`: missing `BRAVE_API_KEY` while strict preflight is enabled.
- `invalid_arguments`: invalid `query`, `count/topK`, `content`, or `url` shape/value.
- `planner_invalid_search_query`: planner could not derive a safe compact query
  for search execution; refine the prompt or provide a direct concise query.
- `timed_out`: operation exceeded runtime deadline.
- `auth_error`: upstream auth/permission rejected.
- `rate_limited`: upstream throttling response.
- `upstream_unavailable`: transient upstream 5xx conditions.
- `network_error`: connectivity or DNS failures.
