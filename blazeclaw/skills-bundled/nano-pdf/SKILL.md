---
name: nano-pdf
description: Edit PDFs with natural-language instructions using the nano-pdf CLI.
homepage: https://pypi.org/project/nano-pdf/
metadata:
  {
    "openclaw":
      {
        "emoji": "📄",
        "requires": { "bins": ["nano-pdf"] },
        "install":
          [
            {
              "id": "uv",
              "kind": "uv",
              "package": "nano-pdf",
              "bins": ["nano-pdf"],
              "label": "Install nano-pdf (uv)",
            },
          ],
      },
  }
---

# nano-pdf

Use `nano-pdf` in a two-step flow: generate an initial report PDF, then apply page-level edits with natural-language instructions.

## Quick start

```bash
nano-pdf edit deck.pdf 1 "Change the title to 'Q3 Results' and fix the typo in the subtitle"
```

## Runtime behavior

For runtime workflows, use:

1. `nano_pdf.generate` to create a draft PDF artifact.
2. `nano_pdf.edit` to edit that existing draft using `inputPath`.

`nano_pdf.edit` now accepts optional routing fields in the BlazeClaw contract:

- `provider`
- `model`
- `baseUrl`
- `apiKeyEnvName`
- `preflightOnly`
- `theme`

Theme presets supported by the fallback renderer:

- `executive`
- `financial`
- `market`
- `healthcare`
- `energy`
- `technology`

If `theme` is omitted, BlazeClaw selects a preset heuristically from the generated report content.

The fallback renderer also now right-aligns numeric table columns when appropriate and can extract KPI cards from metric-heavy narrative paragraphs.

Current runtime routing behavior:

- Gemini requests still use the external Gemini-only `nano-pdf` CLI.
- Local requests (`local`, `llama.cpp`, `ollama`, `openai-compatible`) now use a BlazeClaw-managed fallback route instead of being passed to the external CLI.
- The bridge can inherit provider/model defaults from BlazeClaw config and resolve local base URLs from environment variables.
- `preflightOnly=true` validates routing and connectivity without modifying the PDF.

Notes:

- `nano_pdf.edit` is not a text-to-PDF generator. It requires an existing `inputPath`.
- The local fallback rewrites the whole PDF as a text-centric report document; it is not a pixel-perfect in-place edit of existing artwork.
- The fallback renderer now adds stronger section layout, metadata header rows, executive-summary callouts, KPI-style key/value cards, zebra-shaded wrapped table formatting, multi-page output, footer page numbers, and smarter two-column layout for compact structured report sections.
- Page numbers are 0-based or 1-based depending on the tool’s version/config; if the result looks off by one, retry with the other.
- Always sanity-check the output PDF before sending it out.
