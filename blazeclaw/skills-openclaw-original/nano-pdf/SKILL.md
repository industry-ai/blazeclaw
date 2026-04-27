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

Use `nano-pdf` tools to generate a draft PDF from text, then apply page-level edits with natural-language instructions.

## Quick start

```bash
nano-pdf edit deck.pdf 1 "Change the title to 'Q3 Results' and fix the typo in the subtitle"
```

## Runtime tool contract

- Tool id: `nano_pdf.generate`
  - Purpose: create an initial PDF artifact from report text for downstream edits.
  - Required args:
    - `content` (text body to render)
    - `outputPath` (target draft PDF path)
  - Optional args:
    - `title` (document title)

- Tool id: `nano_pdf.edit`
  - Purpose: edit an existing PDF artifact.
  - Required args:
    - `inputPath` (existing PDF file path)
    - `pageIndex` (integer index of page to edit)
    - `instruction` (natural-language edit instruction)
  - Optional args:
    - `outputPath` (target output path; defaults to `*.edited.pdf` beside input)

### Ordered workflow pattern

1. Call `nano_pdf.generate` to create a draft file (for example, `/tmp/Battery_Report_draft.pdf`).
2. Call `nano_pdf.edit` with:
   - `inputPath=/tmp/Battery_Report_draft.pdf`
   - `pageIndex=0` (or adjusted index)
   - `instruction=<formatting changes>`
   - `outputPath=/tmp/Battery_Report.pdf`

## Failure modes

- `missing_dependency`
  - `nano-pdf` binary is unavailable on PATH.
  - Install with: `uv pip install nano-pdf`
- `invalid_args`
  - One or more required args are missing or unsafe, or `inputPath` is not an existing PDF.
  - Check path values, page index, and instruction text.
- `execution_failed`
  - CLI invocation failed or returned non-success status.
  - Review tool output for stderr/CLI details.

Notes:

- Page numbers are 0-based or 1-based depending on CLI version/config; if output looks off by one page, retry with the other index base.
- Always sanity-check the output PDF before sending it out.
