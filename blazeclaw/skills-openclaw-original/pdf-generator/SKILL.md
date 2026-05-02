---
name: PDF Generator
slug: pdf-generator
version: 1.0.1
homepage: https://clawic.com/skills/pdf-generator
description: Generate professional PDFs from Markdown, HTML, data, or code. Reports, invoices, contracts, and documents with best practices.
metadata: {"clawdbot":{"emoji":"📄","requires":{"bins":[]},"os":["linux","darwin","win32"]}}
command-dispatch: tool
command-tool: pdf_generator.generate
command-arg-mode: raw
command-arg-schema: schema://pdf_generator.generate.args.v1
command-result-schema: schema://pdf_generator.generate.result.v1
command-idempotency-hint: safe
command-retry-policy-hint: transient-runtime
command-requires-approval: false
---

## When to Use

User needs to create, generate, or export PDF documents. Agent handles document generation from multiple sources (Markdown, HTML, JSON, templates), formatting, styling, and batch processing.

## Scope

This skill:
- Provides code patterns and implementation guidance for PDF generation
- Explains tool selection, CSS for print, and document structure
- Supports runtime execution through `pdf_generator.generate` for Markdown→PDF conversion

Runtime execution behavior:
- Reads input Markdown from the current workspace
- Writes output PDF to the requested workspace path
- Does not perform network requests

## Quick Reference

| Topic | File |
|-------|------|
| Tool selection | `tools.md` |
| Document types | `templates.md` |
| Advanced operations | `advanced.md` |

## Core Rules

### 1. Choose the Right Tool

| Source | Best Tool | Why |
|--------|-----------|-----|
| Markdown | pandoc | Native support, TOC, templates |
| HTML/CSS | weasyprint | Best CSS support, no LaTeX |
| Data/JSON | reportlab | Programmatic, precise control |
| Simple text | fpdf2 | Lightweight, fast |

**Default recommendation:** weasyprint for most HTML-based documents.

### 2. Structure Before Style

```python
# CORRECT: semantic structure
html = """
<article>
  <header><h1>Report Title</h1></header>
  <section>
    <h2>Summary</h2>
    <p>Content...</p>
  </section>
</article>
"""

# WRONG: style-first approach
html = "<div style='font-size:24px'>Report Title</div>"
```

### 3. Handle Page Breaks Explicitly

```css
/* Force page break before */
.new-page { page-break-before: always; }

/* Keep together */
.keep-together { page-break-inside: avoid; }

/* Headers never orphaned */
h2, h3 { page-break-after: avoid; }
```

### 4. Always Set Metadata

```python
# Example pattern for weasyprint
html = """
<html>
<head>
  <title>Document Title</title>
  <meta name="author" content="Author Name">
</head>
...
"""
```

### 5. Use Print-Optimized CSS

```css
@media print {
  body {
    font-family: 'Georgia', serif;
    font-size: 11pt;
    line-height: 1.5;
  }
  
  @page {
    size: A4;
    margin: 2cm;
  }
  
  .no-print { display: none; }
}
```

### 6. Validate Output

After generating any PDF:
1. Check file size (0 bytes = failed)
2. Open and verify page count
3. Verify fonts render correctly

## Common Traps

| Trap | Consequence | Fix |
|------|-------------|-----|
| Missing fonts | Fallback to defaults | Use web-safe fonts |
| Absolute image paths | Images missing | Use relative paths |
| No page size | Unpredictable layout | Set `@page { size: A4; }` |
| Large images | Huge files | Compress before use |

## Security & Privacy

**Data stays local:**
- PDF generation runs locally through BlazeClaw runtime
- No data sent externally

**Runtime constraints:**
- Executes trusted local bridge script under skill directories
- No network requests
- File operations are limited to user-provided workspace paths

## BlazeClaw Chat Invocation

- Slash command: `/pdf_generator <input.md> <output.pdf>`
- Tool dispatch: `pdf_generator.generate`
- Recommended explicit arguments: `input_md`, `output_pdf`, `title`, `author`

## Feedback

- If useful: `clawhub star pdf-generator`
- Stay updated: `clawhub sync`
