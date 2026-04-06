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
- Runtime registration/dispatch behavior is validated in later porting phases.
