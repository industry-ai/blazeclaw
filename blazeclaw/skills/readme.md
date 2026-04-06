## BlazeClaw Skills Overview

This folder contains **local implemented skills** and related assets used by
BlazeClaw.

At runtime, the system can surface skills from three sources:

1. **Runtime-registered skills**
   - Provided by gateway/runtime services.
   - Queried through `gateway.skills.list`.
   - Typically reflect skills that are parsed, validated, and currently known
     to the active runtime.
   - If a runtime skill does not provide `installKind` (or reports
     `installKind=general`), `CSkillView` places it under the
     `runtime-registered -> general` subitem.

2. **Implemented local skills**
   - Discovered from `blazeclaw/skills/*` in the workspace.
   - Useful as fallback visibility when runtime registration is incomplete or
     still initializing.
   - Displayed in `CSkillView` under local categories (for example
     `implemented`).

3. **OpenClaw original copied skills (unmodified)**
   - Discovered from `blazeclaw/skills-openclaw-original/*` in the workspace.
   - Intended for visibility of original upstream skill assets without any
     BlazeClaw-specific modification.
   - Displayed in `CSkillView` under `openclaw-original`.

---

## How Skills Are Configured and Registered

### 1) Local implementation layout

Each skill normally has its own folder under `blazeclaw/skills/<skill-name>/`
with artifacts such as:

- `SKILL.md` (skill description + metadata/frontmatter)
- `_meta.json` (optional metadata)
- `tool-manifest.json` (tool IDs/runtime mapping, if tool-based)
- `tool-contracts.json` (JSON input contracts, if tool-based)
- `scripts/*` (runtime scripts/executors)
- `config.html` (optional WebView2 configuration UI)

### 2) Runtime cataloging/registration

Gateway/runtime services (through `ServiceManager` + `GatewayHost`) build a
runtime skill catalog and expose it through:

- `gateway.skills.list`

`CSkillView::FillSkillView()` consumes this endpoint and groups skills by
category in the tree.

### 3) UI selection behavior

When a skill item is selected in `CSkillView`:

- Metadata payload is routed to `CBlazeClawMFCView`.
- The view surfaces selected skill properties/configuration info via status
  output and bridge payloads (for WebView-side handling).

---

## Implemented Local Skills (Current)

### 1. `imap-smtp-email`

**Purpose:** Email operations over IMAP/SMTP (read/search/fetch/download,
mark read/unread, send/test email, multi-account support).

**Key files:**
- `imap-smtp-email/SKILL.md`
- `imap-smtp-email/tool-manifest.json`
- `imap-smtp-email/tool-contracts.json`
- `imap-smtp-email/scripts/imap.js`
- `imap-smtp-email/scripts/smtp.js`
- `imap-smtp-email/scripts/config.js`
- `imap-smtp-email/config.html`

**Configuration:**
- Canonical: `~/.config/imap-smtp-email/.env`
- Fallback: skill-local `.env`
- MFC WebView2 `config.html` loads the last saved `.env` values on open for
  edit/review before saving.

---

### 2. `self-evolving`

**Purpose:** Continuous improvement/logging skill that records learnings,
errors, feature requests, and policy-related operation outputs.

**Key files:**
- `self-evolving/SKILL.md`
- `self-evolving/.learnings/*`
- `self-evolving/scripts/*`
- `self-evolving/hooks/blazeclaw/*`
- `self-evolving/assets/*`

**Configuration/usage:**
- Primarily file-driven via skill assets and scripts.
- Used for operational learnings, governance/attestation artifacts, and
  remediation workflows.

---

### 3. `self-evolving-cpp`

**Purpose:** C++-side placeholder/integration area for self-evolving skill
hook-related components.

**Current state:**
- Minimal scaffold present (hook directory structure).
- Intended for further native integration growth.

---

### 4. `brave-search`

**Purpose:** Headless web search and readable page-content extraction for
documentation lookup and general fact retrieval workflows.

**Current state:**
- Fully ported through Phase 6 (runtime discovery, chat-callable execution,
  runtime hardening, and contributor documentation clearance).

**Key files:**
- `brave-search/SKILL.md`
- `brave-search/_meta.json`
- `brave-search/package.json`
- `brave-search/scripts/search.js`
- `brave-search/scripts/content.js`
- `brave-search/tool-manifest.json`
- `brave-search/tool-contracts.json`
- `brave-search/TOOL_SURFACE.md`
- `brave-search/.clawhub/origin.json`

**Setup:**
- `cd blazeclaw/skills/brave-search && npm ci`

**Usage highlights:**
- Chat-driven web lookup via `brave_search.search.web`
- URL content extraction via `brave_search.fetch.content`
- Optional strict API-key preflight:
  `BLAZECLAW_BRAVE_REQUIRE_API_KEY=true`

---

## Notes

- Runtime visibility in `CSkillView` should include registered runtime skills,
  implemented local skills, and original OpenClaw copied skills.
- `general` is **not** a separate skill source/type. It is treated as part of
  runtime-registered grouping in the tree (as a subitem).
- If a local skill exists but does not appear as runtime-registered, verify
  its manifest/frontmatter validity and runtime catalog refresh flow.

---

## Quick New Skill Checklist (Contributor Template)

Use this checklist when adding a new skill under `blazeclaw/skills/<name>/`.

### Required files

- [ ] `SKILL.md` with frontmatter (`name`, `description`)
- [ ] `_meta.json` (or explicit note why it is not needed)
- [ ] `scripts/` implementation files (if executable skill)

### Recommended files (tool-based skills)

- [ ] `tool-manifest.json` with stable tool IDs and runtime mapping
- [ ] `tool-contracts.json` for JSON input contracts
- [ ] `TOOL_SURFACE.md` for tool usage/documentation
- [ ] `config.html` if UI configuration is required in MFC/WebView2

### Minimal `SKILL.md` frontmatter template

```yaml
---
name: your-skill-name
description: Short summary of what this skill does.
metadata:
  blazeclaw:
    requires:
      bins:
        - node
---
```

### Minimal `tool-manifest.json` template

```json
{
  "schemaVersion": 1,
  "skill": "your-skill-name",
  "namespace": "your_skill_name",
  "tools": [
    {
      "id": "your_skill_name.example.action",
      "label": "Example Action",
      "category": "general",
      "enabled": true,
      "runtime": {
        "type": "node-cli",
        "entry": "scripts/example.js",
        "command": "run"
      },
      "inputSchemaRef": "tool-contracts.json#/contracts/your_skill_name.example.action"
    }
  ]
}
```

### Minimal `tool-contracts.json` template

```json
{
  "schemaVersion": 1,
  "contracts": {
    "your_skill_name.example.action": {
      "type": "object",
      "properties": {
        "input": {
          "type": "string",
          "description": "Primary input for the example action."
        },
        "account": {
          "type": "string",
          "description": "Optional account selector mapped to --account."
        }
      },
      "required": [
        "input"
      ],
      "additionalProperties": false
    }
  }
}
```
