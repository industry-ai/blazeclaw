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
    - `provider` (explicit provider selector)
    - `model` (explicit model selector)
    - `baseUrl` (future provider endpoint override)
    - `apiKeyEnvName` (environment variable name to source the Gemini API key from)

## Provider behavior

The installed `nano-pdf` CLI currently exposes only a Gemini-backed edit path.
Inspection of the installed package shows:

- `nano-pdf edit --help` describes the command as using Nano Banana / Gemini 3 Pro Image.
- The installed Python package hard-codes model `gemini-3-pro-image-preview`.
- The installed package requires `GEMINI_API_KEY` or an alternate env name mapped into that variable by the BlazeClaw bridge.
- The installed package does not expose native `provider`, `model`, or `baseUrl` switching for local backends.

Because of that limitation, BlazeClaw now supports two execution routes:

1. `gemini-cli`
   - Uses the external `nano-pdf` executable.
   - Supports `provider=gemini` and Gemini aliases only.
   - Requires a configured Gemini API key environment variable.
2. `local-fallback`
   - Activated for `provider=local`, `llama.cpp`, `ollama`, or `openai-compatible`.
   - Does not call the external Gemini-only CLI.
   - Performs bridge-level preflight against a local HTTP-compatible model endpoint.
   - Extracts draft text from the source PDF, asks the local model to rewrite it into a polished brief, and re-renders the full output PDF as a fallback document.

### BlazeClaw config inheritance

If `provider`, `model`, or `baseUrl` are not supplied explicitly, the bridge now attempts to inherit defaults from BlazeClaw configuration files:

- `blazeclaw/blazeclaw.conf`
- `blazeclaw/BlazeClawMfc/blazeclaw.conf`
- `blazeclaw/BlazeClawMfc/src/config/blazeclaw.conf`

Current repo defaults resolve to:

- `chat.activeProvider=local`
- `chat.localModel.provider=llama.cpp`
- `chat.activeModel=llama/gemma-4-E2B-it`

For local fallback base URL resolution, the bridge checks in order:

- explicit `baseUrl`
- `NANO_PDF_LOCAL_BASE_URL`
- `OPENAI_BASE_URL`
- `OLLAMA_HOST`
- fallback default `http://127.0.0.1:11434`

### Preflight behavior

Set `preflightOnly=true` on `nano_pdf.edit` to validate routing without performing the edit.
The preflight response reports:

- resolved provider and model
- chosen route (`gemini-cli` or `local-fallback`)
- chosen backend (`openai-compatible` or `ollama`) for local mode
- base URL and probe URL
- available models when exposed by the local endpoint
- config file sources used for default inheritance

Structured preflight errors now include:

- `provider_not_configured`
- `local_model_unreachable`
- `local_model_not_available`
- `unsupported_model`
- `unsupported_base_url`
- `unsupported_provider`

### Local fallback limitations

The local fallback path is intentionally explicit about its behavior:

- it is a whole-document rerender, not an in-place visual patch of existing PDF artwork
- it depends on a reachable local HTTP-compatible endpoint
- it works best with text-centric reports generated through `nano_pdf.generate`
- it avoids silent fallback to Gemini when the user asked for local execution
- it now applies richer document formatting for headings, bullets, numbered lists, and simple table-like rows, but it still remains a heuristic text-oriented renderer

### Fallback renderer formatting behavior

The handcrafted fallback renderer now attempts to improve document readability by:

- using a stronger title/header treatment on the first and subsequent pages
- adding a metadata header row when the report begins with compact `key: value` lines
- recognizing a broader set of section headings such as summary, outlook, valuation, forecast, and risk sections
- promoting `Executive Summary` content into a callout-style summary panel
- wrapping paragraphs into readable lines instead of emitting a single dense text block
- indenting bullets and numbered lists
- rendering compact key/value clusters as boxed KPI-style cards
- parsing pipe-delimited, multi-space, and trailing-value financial rows into table structures
- rendering simple `key: value` groups and table rows as monospace report tables
- balancing wrapped table rows more cleanly for narrow columns
- applying zebra-style body shading for table readability
- adding footer page numbers and stronger page chrome
- using smarter two-column treatment for compact list-heavy sections when text width allows it
- splitting long documents across multiple pages

This improves business-brief output quality while keeping the fallback self-contained and independent of the Gemini-only external CLI.

### Theme presets

The fallback renderer now supports theme presets:

- `executive`
- `financial`
- `market`
- `healthcare`
- `energy`
- `technology`

Use `theme` on `nano_pdf.edit` to select a preset explicitly. If omitted, BlazeClaw chooses a theme heuristically from the report title, metadata, and early-section content.

Theme presets currently influence:

- header and subheader fill tone
- metadata row fill
- summary panel styling
- KPI card fill and border tone
- table header and alternating row shading
- report label shown in the page chrome

### Financial tables and KPI extraction

The fallback renderer now includes additional heuristics for structured metric-heavy reports:

- numeric-heavy table columns are right-aligned when the column contains financial-style values
- financial rows can be detected from pipe-delimited, multi-space, or trailing-value text patterns
- narrative paragraphs containing multiple metric/value statements can be promoted into KPI cards automatically
- the original narrative paragraph is still preserved so the report retains explanatory context

## Ordered workflow pattern

1. Call `nano_pdf.generate` to create a draft file (for example, `/tmp/Battery_Report_draft.pdf`).
2. Call `nano_pdf.edit` with:
   - `inputPath=/tmp/Battery_Report_draft.pdf`
   - `pageIndex=0` (or adjusted index)
   - `instruction=<formatting changes>`
   - `outputPath=/tmp/Battery_Report.pdf`
3. For explicit Gemini execution, optionally add:
   - `provider=gemini`
   - `model=gemini-3-pro-image-preview`
   - `apiKeyEnvName=GEMINI_API_KEY`

### Example: explicit Gemini mode

```json
{
  "toolId": "nano_pdf.edit",
  "inputPath": "/tmp/Battery_Report_draft.pdf",
  "pageIndex": 0,
  "instruction": "Apply a polished business-report layout with clear headings and executive-summary emphasis.",
  "outputPath": "/tmp/Battery_Report.pdf",
  "provider": "gemini",
  "model": "gemini-3-pro-image-preview",
  "apiKeyEnvName": "GEMINI_API_KEY"
}
```

### Example: local-model request behavior

```json
{
  "toolId": "nano_pdf.edit",
  "inputPath": "/tmp/Battery_Report_draft.pdf",
  "pageIndex": 0,
  "instruction": "Format this as a professional battery-market brief.",
  "outputPath": "/tmp/Battery_Report.pdf",
  "provider": "llama.cpp",
  "model": "llama/gemma-4-E2B-it",
  "baseUrl": "http://127.0.0.1:11434"
}
```

Current result:

- BlazeClaw does not launch the Gemini-only external CLI for this request.
- It first performs local preflight against the requested base URL.
- If the endpoint is reachable and the model is available, BlazeClaw executes the `local-fallback` path and rewrites the entire PDF as a polished report document.
- If the endpoint is unreachable, the request fails with `local_model_unreachable`.
- If the endpoint is reachable but does not expose the requested model, the request fails with `local_model_not_available`.

### Example: preflight-only validation

```json
{
  "toolId": "nano_pdf.edit",
  "inputPath": "/tmp/Battery_Report_draft.pdf",
  "pageIndex": 0,
  "instruction": "Format this as a professional battery-market brief.",
  "provider": "llama.cpp",
  "model": "llama/gemma-4-E2B-it",
  "preflightOnly": true
}
```

Use this to validate local routing before attempting the actual PDF rewrite.

## Failure modes

- `missing_dependency`
  - `nano-pdf` binary is unavailable on PATH.
  - Install with: `uv pip install nano-pdf`
- `invalid_args`
  - One or more required args are missing or unsafe, or `inputPath` is not an existing PDF.
  - Check path values, page index, and instruction text.
- `provider_not_configured`
  - The selected provider is missing required configuration.
  - For Gemini, configure `GEMINI_API_KEY` or pass `apiKeyEnvName` pointing at an existing environment variable.
  - For local mode, supply or expose a compatible base URL and model through explicit args or inherited BlazeClaw config.
- `local_model_unreachable`
  - The local fallback endpoint could not be reached.
  - Verify `baseUrl`, `NANO_PDF_LOCAL_BASE_URL`, `OPENAI_BASE_URL`, or `OLLAMA_HOST`, and ensure the service is listening.
- `local_model_not_available`
  - The local endpoint responded, but the requested model was not present.
  - Check the model list exposed by the endpoint and align the requested `model`.
- `unsupported_provider`
  - The request asked for a provider outside the supported Gemini/local fallback set.
  - Supported values are `gemini`, `local`, `llama.cpp`, `ollama`, and `openai-compatible`.
- `unsupported_model`
  - The request asked for an unsupported model for the selected route.
  - Gemini mode only supports `gemini-3-pro-image-preview`; local mode depends on endpoint-exposed models.
- `unsupported_base_url`
  - The request supplied `baseUrl` while still choosing the Gemini CLI route.
  - Use a local provider to activate the bridge-managed fallback path.
- `execution_failed`
  - CLI invocation failed or the local fallback generation/render step failed.
  - Review tool output for stderr, endpoint responses, or fallback rendering details.

Notes:

- Page numbers are 0-based or 1-based depending on CLI version/config; if output looks off by one page, retry with the other index base.
- Always sanity-check the output PDF before sending it out.
- Local `llama/gemma-4-E2B-it` integration requires either a future `nano-pdf` release with backend selection support or a BlazeClaw-managed replacement editing path.