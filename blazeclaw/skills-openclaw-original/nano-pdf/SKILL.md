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

Use `nano-pdf` to apply edits to a specific page in a PDF using a natural-language instruction.

## Quick start

```bash
nano-pdf edit deck.pdf 1 "Change the title to 'Q3 Results' and fix the typo in the subtitle"
```

## Runtime tool contract

- Tool id: `nano_pdf.edit`
- Required args:
  - `inputPath` (PDF file path)
  - `pageIndex` (integer index of page to edit)
  - `instruction` (natural-language edit instruction)
- Optional args:
  - `outputPath` (target output path; defaults to `*.edited.pdf` beside input)

## Failure modes

- `missing_dependency`
  - `nano-pdf` binary is unavailable on PATH.
  - Install with: `uv pip install nano-pdf`
- `invalid_args`
  - One or more required args are missing or unsafe.
  - Check path values, page index, and instruction text.
- `execution_failed`
  - CLI invocation failed or returned non-success status.
  - Review tool output for stderr/CLI details.

Notes:

- Page numbers are 0-based or 1-based depending on CLI version/config; if output looks off by one page, retry with the other index base.
- Always sanity-check the output PDF before sending it out.
