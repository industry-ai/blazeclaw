import base64
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
from datetime import datetime
from pathlib import Path


DEFAULT_PROVIDER = "gemini"
DEFAULT_GEMINI_MODEL = "gemini-3-pro-image-preview"
DEFAULT_GEMINI_API_KEY_ENV = "GEMINI_API_KEY"
DEFAULT_LOCAL_BASE_URL = "http://127.0.0.1:11434"
DEFAULT_OPENAI_API_KEY_ENV = "OPENAI_API_KEY"
DEFAULT_REPORT_THEME = "executive"
CONFIG_CANDIDATE_PATHS = (
    Path("blazeclaw/blazeclaw.conf"),
    Path("blazeclaw/BlazeClawMfc/blazeclaw.conf"),
    Path("blazeclaw/BlazeClawMfc/src/config/blazeclaw.conf"),
)
SECTION_HEADING_PATTERNS = {
    "executive summary",
    "summary",
    "key findings",
    "key takeaways",
    "highlights",
    "implications",
    "recommendations",
    "conclusion",
    "market outlook",
    "financial outlook",
    "financial summary",
    "risk overview",
    "risks",
    "next steps",
    "valuation",
    "forecast",
    "opportunities",
    "operations",
    "strategy",
    "appendix",
}
FINANCIAL_VALUE_PATTERN = re.compile(
    r"^[+-]?(?:\$|€|£|¥)?\d[\d,]*(?:\.\d+)?(?:%|x|bn|b|m|k|million|billion|trillion)?$",
    re.IGNORECASE,
)
NARRATIVE_KPI_PATTERN = re.compile(
    r"(?P<label>[A-Za-z][A-Za-z0-9 /&()\-]{2,32}?)\s+"
    r"(?:was|were|reached|hit|stood at|came in at|improved to|declined to|rose to|grew to|expanded to|totaled|landed at|ended at|increased to|decreased to|is|are)\s+"
    r"(?P<value>[+-]?(?:\$|€|£|¥)?\d[\d,]*(?:\.\d+)?(?:%|x|bn|b|m|k| million| billion| trillion)?)",
    re.IGNORECASE,
)
SECTOR_KPI_KEYWORDS = {
    "financial": ["revenue", "ebitda", "margin", "cash flow", "eps", "earnings", "valuation"],
    "market": ["market share", "adoption", "penetration", "competition", "demand", "commercialization"],
    "healthcare": ["patients", "trial enrollment", "efficacy", "response rate", "approval", "dosage"],
    "energy": ["capacity", "yield", "cost per kwh", "utilization", "generation", "storage"],
    "technology": ["arr", "mau", "dau", "latency", "throughput", "retention", "inference cost"],
}
CURRENCY_SYMBOL_MAP = {
    "$": "USD",
    "€": "EUR",
    "£": "GBP",
    "¥": "CNY",
}
TREND_PANEL_PATTERNS = [
    re.compile(r"(?P<label>[A-Za-z][A-Za-z0-9 /&()\-]{2,28}?)\s+(?:up|rose|grew|increased)\s+(?:to\s+)?(?P<value>[+-]?(?:\$|€|£|¥)?\d[\d,]*(?:\.\d+)?(?:%|x|bn|b|m|k)?)", re.IGNORECASE),
    re.compile(r"(?P<label>[A-Za-z][A-Za-z0-9 /&()\-]{2,28}?)\s+(?:down|fell|declined|decreased)\s+(?:to\s+)?(?P<value>[+-]?(?:\$|€|£|¥)?\d[\d,]*(?:\.\d+)?(?:%|x|bn|b|m|k)?)", re.IGNORECASE),
]
THEME_PRESETS = {
    "executive": {
        "header_fill": 0.94,
        "subheader_fill": 0.96,
        "meta_fill": 0.97,
        "summary_fill": 0.93,
        "summary_border": 0.80,
        "kpi_fill": 0.95,
        "kpi_border": 0.80,
        "table_header_fill": 0.90,
        "table_alt_fill": 0.97,
        "label": "Executive Brief",
    },
    "financial": {
        "header_fill": 0.90,
        "subheader_fill": 0.94,
        "meta_fill": 0.95,
        "summary_fill": 0.90,
        "summary_border": 0.70,
        "kpi_fill": 0.92,
        "kpi_border": 0.72,
        "table_header_fill": 0.86,
        "table_alt_fill": 0.95,
        "label": "Financial Report",
    },
    "market": {
        "header_fill": 0.92,
        "subheader_fill": 0.95,
        "meta_fill": 0.96,
        "summary_fill": 0.91,
        "summary_border": 0.76,
        "kpi_fill": 0.94,
        "kpi_border": 0.78,
        "table_header_fill": 0.88,
        "table_alt_fill": 0.96,
        "label": "Market Brief",
    },
    "healthcare": {
        "header_fill": 0.91,
        "subheader_fill": 0.95,
        "meta_fill": 0.97,
        "summary_fill": 0.92,
        "summary_border": 0.74,
        "kpi_fill": 0.94,
        "kpi_border": 0.76,
        "table_header_fill": 0.87,
        "table_alt_fill": 0.96,
        "label": "Healthcare Brief",
    },
    "energy": {
        "header_fill": 0.89,
        "subheader_fill": 0.93,
        "meta_fill": 0.95,
        "summary_fill": 0.90,
        "summary_border": 0.72,
        "kpi_fill": 0.91,
        "kpi_border": 0.74,
        "table_header_fill": 0.85,
        "table_alt_fill": 0.94,
        "label": "Energy Outlook",
    },
    "technology": {
        "header_fill": 0.93,
        "subheader_fill": 0.96,
        "meta_fill": 0.97,
        "summary_fill": 0.92,
        "summary_border": 0.78,
        "kpi_fill": 0.94,
        "kpi_border": 0.79,
        "table_header_fill": 0.89,
        "table_alt_fill": 0.96,
        "label": "Technology Brief",
    },
}
GEMINI_PROVIDER_ALIASES = {
    "gemini": DEFAULT_PROVIDER,
    "google": DEFAULT_PROVIDER,
    "google-genai": DEFAULT_PROVIDER,
    "nano-banana": DEFAULT_PROVIDER,
}
LOCAL_PROVIDER_ALIASES = {
    "local": "local",
    "llama.cpp": "llama.cpp",
    "llamacpp": "llama.cpp",
    "ollama": "ollama",
    "openai-compatible": "openai-compatible",
    "openai_compatible": "openai-compatible",
}
LOCAL_PROVIDER_HINTS = set(LOCAL_PROVIDER_ALIASES.values())
LOCAL_BACKEND_PRIORITY = {
    "local": ("openai-compatible", "ollama"),
    "llama.cpp": ("openai-compatible", "ollama"),
    "ollama": ("ollama",),
    "openai-compatible": ("openai-compatible",),
}
PDF_TEXT_PATTERN = re.compile(rb"\(((?:\\.|[^\\)])*)\)\s*Tj")


def _error(error_code: str, message: str, details: str = ""):
    payload = {
        "ok": False,
        "errorCode": error_code,
        "message": message,
    }
    if details:
        payload["details"] = details
    print(json.dumps(payload, ensure_ascii=False, indent=2))
    return 1


def _success(**payload):
    payload.setdefault("ok", True)
    print(json.dumps(payload, ensure_ascii=False, indent=2))
    return 0


def _derive_output_path(input_path: Path) -> Path:
    if input_path.suffix.lower() == ".pdf":
        return input_path.with_name(f"{input_path.stem}.edited.pdf")
    return input_path.with_name(f"{input_path.name}.edited.pdf")


def _escape_pdf_text(value: str) -> str:
    return value.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")


def _chunk_text(text: str, max_chars: int):
    value = (text or "").strip()
    if not value:
        return [""]

    words = value.split()
    if not words:
        return [value[:max_chars]]

    lines = []
    current = words[0]
    for word in words[1:]:
        candidate = f"{current} {word}"
        if len(candidate) <= max_chars:
            current = candidate
            continue
        lines.append(current)
        if len(word) <= max_chars:
            current = word
            continue
        segments = [word[i:i + max_chars] for i in range(0, len(word), max_chars)]
        lines.extend(segments[:-1])
        current = segments[-1]
    lines.append(current)
    return lines


def _build_pdf_document(page_streams):
    if not page_streams:
        page_streams = [b""]

    page_count = len(page_streams)
    font_map = {
        "F1": 2 + (page_count * 2) + 1,
        "F2": 2 + (page_count * 2) + 2,
        "F3": 2 + (page_count * 2) + 3,
    }

    objects = []
    page_ids = []
    content_ids = []
    next_id = 3
    for stream in page_streams:
        page_id = next_id
        content_id = next_id + 1
        page_ids.append(page_id)
        content_ids.append(content_id)
        next_id += 2
        objects.append(
            (
                page_id,
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                f"/Resources << /Font << /F1 {font_map['F1']} 0 R /F2 {font_map['F2']} 0 R /F3 {font_map['F3']} 0 R >> >> "
                f"/Contents {content_id} 0 R >>",
            )
        )
        objects.append(
            (
                content_id,
                b"<< /Length " + str(len(stream)).encode("ascii") + b" >>\nstream\n"
                + stream + b"\nendstream",
            )
        )

    kids = " ".join(f"{page_id} 0 R" for page_id in page_ids)
    objects.insert(0, (2, f"<< /Type /Pages /Kids [{kids}] /Count {page_count} >>"))
    objects.insert(0, (1, "<< /Type /Catalog /Pages 2 0 R >>"))
    objects.extend(
        [
            (font_map["F1"], "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"),
            (font_map["F2"], "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>"),
            (font_map["F3"], "<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>"),
        ]
    )
    objects.sort(key=lambda item: item[0])

    header = b"%PDF-1.4\n"
    body = bytearray(header)
    xref_positions = [0]
    for obj_id, obj_content in objects:
        xref_positions.append(len(body))
        body.extend(f"{obj_id} 0 obj\n".encode("ascii"))
        if isinstance(obj_content, bytes):
            body.extend(obj_content)
        else:
            body.extend(obj_content.encode("latin-1", errors="replace"))
        body.extend(b"\nendobj\n")

    xref_offset = len(body)
    body.extend(f"xref\n0 {len(xref_positions)}\n".encode("ascii"))
    body.extend(b"0000000000 65535 f \n")
    for pos in xref_positions[1:]:
        body.extend(f"{pos:010d} 00000 n \n".encode("ascii"))
    body.extend(
        f"trailer\n<< /Size {len(xref_positions)} /Root 1 0 R >>\nstartxref\n{xref_offset}\n%%EOF\n".encode(
            "ascii"
        )
    )
    return bytes(body)


def _pdf_text_op(x: int, y: int, text: str, font: str = "F1", size: int = 11):
    return f"BT /{font} {size} Tf {x} {y} Td ({_escape_pdf_text(text)}) Tj ET"


def _pdf_line_op(x1: int, y1: int, x2: int, y2: int):
    return f"0.82 G 0.82 g {x1} {y1} m {x2} {y2} l S 0 G 0 g"


def _is_financial_value(value: str) -> bool:
    candidate = (value or "").strip()
    return bool(candidate and FINANCIAL_VALUE_PATTERN.match(candidate))


def _normalize_currency_value(value: str):
    candidate = (value or "").strip()
    if not candidate:
        return {
            "display": "",
            "currency": "",
            "numeric": "",
        }
    currency = ""
    if candidate[0] in CURRENCY_SYMBOL_MAP:
        currency = CURRENCY_SYMBOL_MAP[candidate[0]]
        numeric = candidate[1:].strip()
    else:
        numeric = candidate
    return {
        "display": candidate,
        "currency": currency,
        "numeric": numeric,
    }


def _split_financial_row(text: str):
    candidate = text.strip()
    if not candidate:
        return None

    pipe_cells = [cell.strip() for cell in candidate.strip("|").split("|") if cell.strip()]
    if len(pipe_cells) >= 2 and any(_is_financial_value(cell) for cell in pipe_cells[1:]):
        return pipe_cells

    segments = [segment.strip() for segment in re.split(r"\s{2,}", candidate) if segment.strip()]
    if len(segments) >= 2 and any(_is_financial_value(segment) for segment in segments[1:]):
        return segments

    tokens = candidate.split()
    for split_index in range(1, len(tokens)):
        right = " ".join(tokens[split_index:])
        if _is_financial_value(right):
            left = " ".join(tokens[:split_index]).strip()
            if left:
                return [left, right]
    return None


def _is_section_heading_text(text: str) -> bool:
    lowered = text.lower().rstrip(":")
    if lowered in SECTION_HEADING_PATTERNS:
        return True
    if re.match(r"^(section|chapter)\s+\d+[:.-]?\s+", lowered):
        return True
    if re.match(r"^(q[1-4]|fy\d{2,4}|h[12])\b", lowered):
        return True
    return False


def _classify_report_line(line: str):
    text = line.strip()
    if not text:
        return "blank"
    if text.count("|") >= 2:
        return "pipe-table"
    if re.fullmatch(r"[\-|:\s]+", text.replace("|", "")):
        return "pipe-separator"
    if _split_financial_row(text):
        return "financial-row"
    if re.match(r"^[A-Za-z0-9 /%&()_-]{1,32}:\s+.+$", text):
        return "kv-row"
    if re.match(r"^(?:[-*]|•)\s+", text):
        return "bullet"
    if re.match(r"^\d+[.)]\s+", text):
        return "numbered"
    lowered = text.lower().rstrip(":")
    if text.startswith("#") or text.endswith(":") or _is_section_heading_text(text):
        return "heading"
    if len(text) <= 42 and text == text.upper() and any(ch.isalpha() for ch in text):
        return "heading"
    if len(text) <= 42 and text == text.title() and text.count(" ") <= 4:
        return "heading"
    return "paragraph"


def _parse_table_rows(lines):
    rows = []
    for line in lines:
        text = line.strip()
        if not text:
            continue
        if re.fullmatch(r"[\-|:\s]+", text.replace("|", "")):
            continue
        financial_row = _split_financial_row(text)
        if financial_row:
            rows.append(financial_row)
            continue
        cells = [cell.strip() for cell in text.strip("|").split("|")]
        cells = [cell for cell in cells if cell]
        if cells:
            rows.append(cells)
    return rows


def _split_cell_lines(text: str, width: int):
    if not text:
        return [""]
    wrapped = _chunk_text(text, max(width, 4))
    return wrapped or [""]


def _format_table_rows(rows, max_columns: int = 3, max_total_width: int = 68):
    normalized = [row[:max_columns] for row in rows if row]
    if not normalized:
        return {
            "column_widths": [],
            "numeric_columns": [],
            "currency_columns": [],
            "rows": [],
        }

    column_count = max(len(row) for row in normalized)
    padded = [row + [""] * (column_count - len(row)) for row in normalized]

    numeric_columns = []
    currency_columns = []
    for index in range(column_count):
        values = [row[index] for row in padded[1:] if row[index].strip()]
        is_numeric = bool(values) and all(_is_financial_value(value) for value in values)
        numeric_columns.append(is_numeric)
        currencies = {info["currency"] for info in (_normalize_currency_value(value) for value in values) if info["currency"]}
        currency_columns.append(sorted(currencies))

    content_scores = []
    for index in range(column_count):
        display_values = []
        for value in [row[index] for row in padded]:
            normalized_value = _normalize_currency_value(value)
            display = normalized_value["numeric"] if numeric_columns[index] else value
            display_values.append(display or value)
        max_len = max(len(value) for value in display_values)
        avg_len = sum(len(value) for value in display_values) / max(len(display_values), 1)
        preferred = int(round((max_len * 0.65) + (avg_len * 0.35)))
        lower_bound = 9 if numeric_columns[index] else 10
        upper_bound = 20 if numeric_columns[index] else 28
        content_scores.append(min(max(preferred, lower_bound), upper_bound))

    total_width = sum(content_scores) + ((column_count - 1) * 3)
    while total_width > max_total_width and any(width > 8 for width in content_scores):
        max_index = max(range(column_count), key=lambda idx: content_scores[idx])
        content_scores[max_index] -= 1
        total_width = sum(content_scores) + ((column_count - 1) * 3)

    formatted_rows = []
    for row_index, row in enumerate(padded):
        cell_lines = []
        for index, cell in enumerate(row):
            width = content_scores[index]
            normalized_value = _normalize_currency_value(cell)
            rendered_value = normalized_value["numeric"] if numeric_columns[index] and row_index > 0 else cell
            wrapped = _split_cell_lines(rendered_value, width)
            cell_lines.append(wrapped)
        row_height = max(len(lines) for lines in cell_lines)
        expanded = []
        for line_index in range(row_height):
            cells = []
            for column_index, lines in enumerate(cell_lines):
                width = content_scores[column_index]
                value = lines[line_index] if line_index < len(lines) else ""
                prefix = ""
                if numeric_columns[column_index] and row_index > 0 and line_index == 0 and currency_columns[column_index]:
                    prefix = f"[{','.join(currency_columns[column_index])}] "
                cell_value = f"{prefix}{value}" if prefix else value
                if row_index == 0 and column_index == 0:
                    padded_value = cell_value.center(width)
                elif numeric_columns[column_index] and row_index > 0:
                    padded_value = cell_value.rjust(width)
                else:
                    padded_value = cell_value.ljust(width)
                cells.append(padded_value[:max(width, len(padded_value))])
            expanded.append(" | ".join(cells).rstrip())
        formatted_rows.append(expanded)

    return {
        "column_widths": content_scores,
        "numeric_columns": numeric_columns,
        "currency_columns": currency_columns,
        "rows": formatted_rows,
    }


def _extract_metadata_rows(lines):
    metadata = []
    remaining = list(lines)
    while remaining and len(metadata) < 4:
        text = remaining[0].strip()
        if not text:
            remaining.pop(0)
            continue
        if not re.match(r"^[A-Za-z0-9 /%&()_\-.]{1,28}:\s+.+$", text):
            break
        key, value = text.split(":", 1)
        metadata.append((key.strip(), value.strip()))
        remaining.pop(0)
    return metadata, remaining


def _choose_report_theme(title: str, metadata, blocks, requested_theme: str = ""):
    if requested_theme:
        return requested_theme

    title_text = (title or "").lower()
    metadata_text = " ".join(f"{key} {value}" for key, value in metadata).lower()
    block_text = " ".join(
        block.get("text", "") if isinstance(block, dict) else ""
        for block in blocks[:8]
    ).lower()
    corpus = f"{title_text} {metadata_text} {block_text}"

    if any(token in corpus for token in ["revenue", "ebitda", "margin", "cash flow", "valuation", "earnings"]):
        return "financial"
    if any(token in corpus for token in ["market", "share", "competition", "adoption", "industry", "commercialization"]):
        return "market"
    if any(token in corpus for token in ["hospital", "clinical", "patient", "drug", "therapy", "biotech"]):
        return "healthcare"
    if any(token in corpus for token in ["battery", "grid", "oil", "gas", "power", "renewable", "energy"]):
        return "energy"
    if any(token in corpus for token in ["software", "semiconductor", "cloud", "ai", "platform", "saas"]):
        return "technology"
    return DEFAULT_REPORT_THEME


def _normalize_kpi_label(label: str) -> str:
    value = re.sub(r"\b(the|our|its|their)\b", "", label, flags=re.IGNORECASE)
    value = re.sub(r"\s+", " ", value).strip(" -:;")
    if not value:
        return "Metric"
    return value.title() if value.islower() else value


def _infer_kpi_sector(label: str, text: str):
    corpus = f"{label} {text}".lower()
    for sector, keywords in SECTOR_KPI_KEYWORDS.items():
        if any(keyword in corpus for keyword in keywords):
            return sector
    return ""


def _extract_narrative_kpis(text: str):
    candidate = (text or "").strip()
    if not candidate:
        return []

    kpis = []
    seen = set()
    for match in NARRATIVE_KPI_PATTERN.finditer(candidate):
        label = _normalize_kpi_label(match.group("label"))
        value = match.group("value").strip()
        if len(label) > 28 or not _is_financial_value(value):
            continue
        fingerprint = (label.lower(), value.lower())
        if fingerprint in seen:
            continue
        seen.add(fingerprint)
        sector = _infer_kpi_sector(label, candidate)
        display_label = f"[{sector.title()}] {label}" if sector else label
        kpis.append([display_label, value])
        if len(kpis) >= 6:
            break
    return kpis


def _extract_trend_panel(text: str):
    candidate = (text or "").strip()
    if not candidate:
        return None

    entries = []
    seen = set()
    for pattern in TREND_PANEL_PATTERNS:
        for match in pattern.finditer(candidate):
            label = _normalize_kpi_label(match.group("label"))
            value = match.group("value").strip()
            fingerprint = (label.lower(), value.lower())
            if fingerprint in seen or not _is_financial_value(value):
                continue
            seen.add(fingerprint)
            entries.append([label[:24], value])
            if len(entries) >= 4:
                break
        if len(entries) >= 4:
            break

    if len(entries) < 2:
        return None
    return {
        "kind": "trend-panel",
        "entries": entries,
    }


def _gather_report_blocks(lines):
    metadata, working_lines = _extract_metadata_rows(lines)
    blocks = []
    index = 0
    while index < len(working_lines):
        current = working_lines[index].rstrip()
        kind = _classify_report_line(current)
        if kind == "blank":
            blocks.append({"kind": "spacer"})
            index += 1
            continue
        if kind in {"pipe-table", "pipe-separator"}:
            table_lines = []
            while index < len(working_lines) and _classify_report_line(working_lines[index]) in {"pipe-table", "pipe-separator"}:
                table_lines.append(working_lines[index])
                index += 1
            rows = _parse_table_rows(table_lines)
            if rows:
                blocks.append({"kind": "table", "rows": rows})
            continue
        if kind == "kv-row":
            kv_rows = []
            while index < len(working_lines) and _classify_report_line(working_lines[index]) == "kv-row":
                key, value = working_lines[index].split(":", 1)
                kv_rows.append([key.strip(), value.strip()])
                index += 1
            if len(kv_rows) >= 3 and all(len(key) <= 18 for key, _ in kv_rows):
                blocks.append({"kind": "kpi-grid", "rows": kv_rows})
            elif len(kv_rows) >= 2:
                blocks.append({"kind": "table", "rows": kv_rows})
            else:
                key, value = kv_rows[0]
                blocks.append({"kind": "paragraph", "text": f"{key}: {value}"})
            continue
        if kind == "heading":
            heading_text = current.lstrip("#").strip().rstrip(":")
            lowered_heading = heading_text.lower()
            blocks.append({"kind": "heading", "text": heading_text})
            index += 1
            if lowered_heading == "executive summary":
                summary_parts = []
                while index < len(working_lines):
                    probe = working_lines[index].rstrip()
                    probe_kind = _classify_report_line(probe)
                    if probe_kind in {"blank"}:
                        index += 1
                        if summary_parts:
                            break
                        continue
                    if probe_kind != "paragraph":
                        break
                    summary_parts.append(probe.strip())
                    index += 1
                if summary_parts:
                    blocks.append({"kind": "summary-panel", "text": " ".join(summary_parts)})
            continue
        if kind == "bullet":
            blocks.append({"kind": "bullet", "text": re.sub(r"^(?:[-*]|•)\s+", "", current).strip()})
            index += 1
            continue
        if kind == "numbered":
            match = re.match(r"^(\d+[.)])\s+(.*)$", current)
            blocks.append({
                "kind": "numbered",
                "label": match.group(1) if match else "1.",
                "text": match.group(2).strip() if match else current,
            })
            index += 1
            continue

        paragraph_lines = [current.strip()]
        index += 1
        while index < len(working_lines) and _classify_report_line(working_lines[index]) == "paragraph":
            paragraph_lines.append(working_lines[index].strip())
            index += 1
        paragraph_text = " ".join(paragraph_lines)
        trend_panel = _extract_trend_panel(paragraph_text)
        if trend_panel:
            blocks.append(trend_panel)
        narrative_kpis = _extract_narrative_kpis(paragraph_text)
        if len(narrative_kpis) >= 2:
            blocks.append({"kind": "kpi-grid", "rows": narrative_kpis})
        blocks.append({"kind": "paragraph", "text": paragraph_text})
    return metadata, blocks


def _should_use_two_columns(column_blocks):
    if len(column_blocks) < 4 or len(column_blocks) > 10:
        return False
    if any(block["kind"] not in {"bullet", "numbered"} for block in column_blocks):
        return False
    average_length = sum(len(block["text"]) for block in column_blocks) / len(column_blocks)
    return average_length <= 72


def _merge_column_candidate(column_blocks):
    if not _should_use_two_columns(column_blocks):
        return None
    return {"kind": "two-column-list", "items": column_blocks}


def _compact_blocks_for_layout(blocks):
    compacted = []
    index = 0
    while index < len(blocks):
        block = blocks[index]
        if block["kind"] == "heading":
            compacted.append(block)
            if index + 1 < len(blocks) and blocks[index + 1]["kind"] == "summary-panel":
                compacted.append(blocks[index + 1])
                index += 2
                continue
            lookahead = []
            probe = index + 1
            while probe < len(blocks) and blocks[probe]["kind"] in {"bullet", "numbered"}:
                lookahead.append(blocks[probe])
                probe += 1
            merged = _merge_column_candidate(lookahead)
            if merged:
                compacted.append(merged)
                index = probe
                continue
            index += 1
            continue
        compacted.append(block)
        index += 1
    return compacted


def _pdf_fill_rect_op(x: int, y: int, width: int, height: int, gray: float = 0.92):
    return f"{gray:.2f} g {x} {y} {width} {height} re f 0 g"


def _pdf_rect_stroke_op(x: int, y: int, width: int, height: int, gray: float = 0.78):
    return f"{gray:.2f} G {x} {y} {width} {height} re S 0 G"


def _estimate_block_height(block):
    kind = block["kind"]
    if kind == "spacer":
        return 8
    if kind == "heading":
        return 32
    if kind == "summary-panel":
        return (len(_chunk_text(block["text"], 72)) * 14) + 28
    if kind == "trend-panel":
        return (len(block["entries"]) * 18) + 28
    if kind == "paragraph":
        return (len(_chunk_text(block["text"], 86)) * 14) + 8
    if kind == "bullet":
        return (len(_chunk_text(block["text"], 76)) * 14) + 6
    if kind == "numbered":
        return (len(_chunk_text(block["text"], 72)) * 14) + 6
    if kind == "table":
        table = _format_table_rows(block["rows"])
        row_units = sum(len(row_lines) for row_lines in table["rows"])
        return (row_units * 13) + 34
    if kind == "kpi-grid":
        card_count = min(len(block["rows"]), 6)
        row_count = (card_count + 1) // 2
        return (row_count * 66) + 10
    if kind == "two-column-list":
        item_heights = []
        for item in block["items"]:
            width = 28 if item["kind"] == "numbered" else 30
            item_heights.append(len(_chunk_text(item["text"], width)) * 13)
        pairs = []
        for idx in range(0, len(item_heights), 2):
            left = item_heights[idx]
            right = item_heights[idx + 1] if idx + 1 < len(item_heights) else 0
            pairs.append(max(left, right))
        return sum(pairs) + (len(pairs) * 6) + 8
    return 20


def _theme_value(theme, key: str, fallback):
    return theme.get(key, fallback)


def _render_page_footer(ops, page_number: int, theme):
    ops.append(_pdf_line_op(54, 42, 558, 42))
    ops.append(_pdf_text_op(54, 26, f"BlazeClaw {_theme_value(theme, 'label', 'Report')}", font="F1", size=8))
    ops.append(_pdf_text_op(500, 26, f"Page {page_number}", font="F1", size=9))


def _render_metadata_row(ops, metadata, y, theme):
    if not metadata:
        return y
    pills = [f"{key}: {value}"[:36] for key, value in metadata[:4]]
    ops.append(_pdf_fill_rect_op(54, y - 6, 504, 18, gray=_theme_value(theme, 'meta_fill', 0.97)))
    x = 62
    for pill in pills:
        ops.append(_pdf_text_op(x, y + 4, pill, font="F1", size=8))
        x += 120
    return y - 24


def _render_summary_panel(ops, text, y, theme):
    wrapped = _chunk_text(text, 72)
    panel_height = (len(wrapped) * 14) + 18
    bottom = y - panel_height
    ops.append(_pdf_fill_rect_op(54, bottom, 504, panel_height, gray=_theme_value(theme, 'summary_fill', 0.93)))
    ops.append(_pdf_rect_stroke_op(54, bottom, 504, panel_height, gray=_theme_value(theme, 'summary_border', 0.80)))
    ops.append(_pdf_text_op(64, y - 10, "Executive Summary", font="F2", size=12))
    text_y = y - 26
    for line in wrapped:
        ops.append(_pdf_text_op(64, text_y, line[:82], font="F1", size=11))
        text_y -= 14
    return bottom - 8


def _render_trend_panel(ops, entries, y, theme):
    panel_height = (len(entries) * 18) + 22
    bottom = y - panel_height
    ops.append(_pdf_fill_rect_op(54, bottom, 504, panel_height, gray=_theme_value(theme, 'summary_fill', 0.92)))
    ops.append(_pdf_rect_stroke_op(54, bottom, 504, panel_height, gray=_theme_value(theme, 'summary_border', 0.78)))
    ops.append(_pdf_text_op(64, y - 10, "Trend Summary", font="F2", size=12))
    row_y = y - 28
    for label, value in entries[:4]:
        bar_width = min(220, max(40, len(value) * 12))
        ops.append(_pdf_text_op(64, row_y, label[:24], font="F1", size=10))
        ops.append(_pdf_fill_rect_op(190, row_y - 6, bar_width, 8, gray=_theme_value(theme, 'kpi_fill', 0.94)))
        ops.append(_pdf_rect_stroke_op(190, row_y - 6, bar_width, 8, gray=_theme_value(theme, 'kpi_border', 0.78)))
        ops.append(_pdf_text_op(420, row_y, value[:18], font="F3", size=9))
        row_y -= 18
    return bottom - 8


def _render_kpi_grid(ops, rows, y, theme):
    card_rows = rows[:6]
    left_x = 54
    right_x = 312
    top_y = y
    row_y = top_y
    for index in range(0, len(card_rows), 2):
        pair = card_rows[index:index + 2]
        for column_index, (key, value) in enumerate(pair):
            card_x = left_x if column_index == 0 else right_x
            ops.append(_pdf_fill_rect_op(card_x, row_y - 54, 246, 48, gray=_theme_value(theme, 'kpi_fill', 0.95)))
            ops.append(_pdf_rect_stroke_op(card_x, row_y - 54, 246, 48, gray=_theme_value(theme, 'kpi_border', 0.80)))
            ops.append(_pdf_text_op(card_x + 10, row_y - 18, key[:28], font="F2", size=10))
            value_lines = _chunk_text(value, 28)[:2]
            text_y = row_y - 34
            for line in value_lines:
                ops.append(_pdf_text_op(card_x + 10, text_y, line[:30], font="F1", size=11))
                text_y -= 12
        row_y -= 60
    return row_y - 4


def _render_report_pages(title: str, blocks, metadata=None, theme_name: str = DEFAULT_REPORT_THEME):
    pages = []
    ops = []
    y = 750
    blocks = _compact_blocks_for_layout(blocks)
    metadata = metadata or []
    generated_date = datetime.now().strftime("%Y-%m-%d")
    theme = THEME_PRESETS.get(theme_name, THEME_PRESETS[DEFAULT_REPORT_THEME])

    def start_new_page(first_page: bool):
        page_ops = []
        current_y = 750
        if first_page:
            page_ops.append(_pdf_fill_rect_op(54, 742, 504, 28, gray=_theme_value(theme, 'header_fill', 0.94)))
            page_ops.append(_pdf_text_op(62, 758, title[:80], font="F2", size=20))
            page_ops.append(_pdf_text_op(62, 744, f"BlazeClaw {_theme_value(theme, 'label', 'Report')}", font="F1", size=9))
            page_ops.append(_pdf_text_op(468, 744, generated_date, font="F1", size=8))
            page_ops.append(_pdf_line_op(54, 738, 558, 738))
            current_y = 712
            current_y = _render_metadata_row(page_ops, metadata, current_y, theme)
        else:
            page_ops.append(_pdf_fill_rect_op(54, 748, 504, 18, gray=_theme_value(theme, 'subheader_fill', 0.96)))
            page_ops.append(_pdf_text_op(62, 758, title[:72], font="F2", size=14))
            page_ops.append(_pdf_text_op(498, 758, generated_date, font="F1", size=8))
            page_ops.append(_pdf_line_op(54, 744, 558, 744))
            current_y = 720
        return page_ops, current_y

    def ensure_space(required_height: int):
        nonlocal ops, y, pages
        if y - required_height >= 58:
            return
        _render_page_footer(ops, len(pages) + 1, theme)
        pages.append("\n".join(ops).encode("latin-1", errors="replace"))
        ops, y = start_new_page(False)

    ops, y = start_new_page(True)
    for block in blocks:
        kind = block["kind"]
        if kind == "spacer":
            y -= 8
            continue
        if kind == "heading":
            ensure_space(_estimate_block_height(block))
            y -= 8
            ops.append(_pdf_text_op(54, y, block["text"][:84], font="F2", size=15))
            y -= 7
            ops.append(_pdf_line_op(54, y, 340, y))
            y -= 12
            continue
        if kind == "summary-panel":
            ensure_space(_estimate_block_height(block))
            y = _render_summary_panel(ops, block["text"], y, theme)
            continue
        if kind == "trend-panel":
            ensure_space(_estimate_block_height(block))
            y = _render_trend_panel(ops, block["entries"], y, theme)
            continue
        if kind == "paragraph":
            wrapped = _chunk_text(block["text"], 86)
            ensure_space(_estimate_block_height(block))
            for line in wrapped:
                ops.append(_pdf_text_op(54, y, line[:96], font="F1", size=11))
                y -= 14
            y -= 4
            continue
        if kind == "bullet":
            wrapped = _chunk_text(block["text"], 76)
            ensure_space(_estimate_block_height(block))
            for idx, line in enumerate(wrapped):
                if idx == 0:
                    ops.append(_pdf_text_op(60, y, "-", font="F2", size=12))
                ops.append(_pdf_text_op(78, y, line[:88], font="F1", size=11))
                y -= 14
            y -= 2
            continue
        if kind == "numbered":
            wrapped = _chunk_text(block["text"], 72)
            ensure_space(_estimate_block_height(block))
            ops.append(_pdf_text_op(54, y, block["label"], font="F2", size=11))
            for line in wrapped:
                ops.append(_pdf_text_op(78, y, line[:86], font="F1", size=11))
                y -= 14
            y -= 2
            continue
        if kind == "two-column-list":
            ensure_space(_estimate_block_height(block))
            left_x = 54
            right_x = 316
            gutter_line_x = 302
            ops.append(_pdf_line_op(gutter_line_x, y + 4, gutter_line_x, y - 56))
            items = block["items"]
            row_y = y
            for index in range(0, len(items), 2):
                pair = items[index:index + 2]
                pair_height = 0
                for column_index, item in enumerate(pair):
                    base_x = left_x if column_index == 0 else right_x
                    label_x = base_x
                    text_x = base_x + 22
                    if item["kind"] == "bullet":
                        ops.append(_pdf_text_op(label_x, row_y, "-", font="F2", size=11))
                        wrapped = _chunk_text(item["text"], 28)
                    else:
                        ops.append(_pdf_text_op(label_x, row_y, item["label"], font="F2", size=10))
                        wrapped = _chunk_text(item["text"], 26)
                    local_height = 0
                    for line in wrapped:
                        ops.append(_pdf_text_op(text_x, row_y - local_height, line[:32], font="F1", size=10))
                        local_height += 13
                    pair_height = max(pair_height, local_height)
                row_y -= pair_height + 6
            y = row_y - 2
            continue
        if kind == "kpi-grid":
            ensure_space(_estimate_block_height(block))
            y = _render_kpi_grid(ops, block["rows"], y, theme)
            continue
        if kind == "table":
            table = _format_table_rows(block["rows"])
            ensure_space(_estimate_block_height(block))
            table_top = y + 6
            ops.append(_pdf_fill_rect_op(54, table_top - 20, 504, 18, gray=_theme_value(theme, 'table_header_fill', 0.90)))
            ops.append(_pdf_line_op(54, table_top, 558, table_top))
            current_y = y - 8
            for row_index, row_lines in enumerate(table["rows"]):
                if row_index > 0:
                    shade_height = max(len(row_lines) * 12, 12)
                    if row_index % 2 == 1:
                        ops.append(_pdf_fill_rect_op(54, current_y - shade_height + 2, 504, shade_height, gray=_theme_value(theme, 'table_alt_fill', 0.97)))
                font = "F2" if row_index == 0 else "F3"
                size = 10 if row_index == 0 else 9
                for row in row_lines:
                    ops.append(_pdf_text_op(58, current_y, row[:94], font=font, size=size))
                    current_y -= 12
                if row_index == 0:
                    ops.append(_pdf_line_op(54, current_y + 3, 558, current_y + 3))
                else:
                    ops.append(_pdf_line_op(54, current_y + 2, 558, current_y + 2))
                current_y -= 2
            y = current_y - 6
            continue

    _render_page_footer(ops, len(pages) + 1, theme)
    pages.append("\n".join(ops).encode("latin-1", errors="replace"))
    return pages


def _build_simple_text_pdf(content: str, title: str = "") -> bytes:
    lines = [line.rstrip() for line in content.splitlines()]
    if not lines:
        lines = [content.strip()]
    lines = [line for line in lines if line]
    if not lines:
        lines = ["(empty report)"]

    body = "\n".join(lines[:80])
    document_title = title.strip() or lines[0][:80] or "Generated Report"
    pages = _render_report_pages(
        document_title,
        [{"kind": "paragraph", "text": body}],
        metadata=[],
        theme_name=DEFAULT_REPORT_THEME,
    )
    return _build_pdf_document(pages)


def _build_report_pdf(content: str, title: str = "", requested_theme: str = "") -> bytes:
    lines = [line.strip() for line in content.splitlines()]
    non_empty = [line for line in lines if line]
    if not non_empty:
        non_empty = ["Generated business brief", "(empty report)"]

    document_title = title.strip() or non_empty[0][:80]
    body_lines = non_empty[1:] if title.strip() and len(non_empty) > 1 else non_empty[1:]
    if not body_lines:
        body_lines = ["(empty report)"]

    metadata, blocks = _gather_report_blocks(body_lines)
    theme_name = _choose_report_theme(document_title, metadata, blocks, requested_theme=requested_theme)
    pages = _render_report_pages(document_title, blocks, metadata=metadata, theme_name=theme_name)
    return _build_pdf_document(pages)


def _safe_resolve_path(raw: str):
    if not isinstance(raw, str) or not raw.strip():
        return None
    value = raw.strip()
    if "\x00" in value:
        return None
    if os.name == "nt" and value.startswith("/tmp/"):
        value = str(Path(os.getenv("TEMP", "C:/tmp")) / value[len("/tmp/"):])
    return Path(value).expanduser().resolve()


def _normalize_base_url(raw: str) -> str:
    if not isinstance(raw, str) or not raw.strip():
        return ""
    value = raw.strip().rstrip("/")
    if "://" not in value:
        value = f"http://{value}"
    return value


def _normalize_provider(raw: str) -> str:
    if not isinstance(raw, str) or not raw.strip():
        return ""
    provider = raw.strip().lower()
    if provider in GEMINI_PROVIDER_ALIASES:
        return GEMINI_PROVIDER_ALIASES[provider]
    return LOCAL_PROVIDER_ALIASES.get(provider, provider)


def _parse_optional_text(parsed, field_name: str) -> str:
    value = parsed.get(field_name, "")
    if value in (None, ""):
        return ""
    if not isinstance(value, str):
        raise ValueError(f"{field_name} must be a string")
    return value.strip()


def _parse_optional_bool(parsed, field_name: str) -> bool:
    value = parsed.get(field_name, False)
    if isinstance(value, bool):
        return value
    raise ValueError(f"{field_name} must be a boolean")


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[4]


def _load_config_file(path: Path):
    values = {}
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def _load_blazeclaw_defaults():
    repo_root = _repo_root()
    merged = {}
    sources = []
    for relative_path in CONFIG_CANDIDATE_PATHS:
        candidate = repo_root / relative_path
        if candidate.exists():
            merged.update(_load_config_file(candidate))
            sources.append(str(candidate))

    active_provider = _normalize_provider(merged.get("chat.activeProvider", ""))
    local_enabled = merged.get("chat.localModel.enabled", "false").lower() == "true"
    local_provider = _normalize_provider(merged.get("chat.localModel.provider", ""))
    if active_provider == "local" and local_enabled:
        inferred_provider = local_provider or "local"
    else:
        inferred_provider = active_provider or DEFAULT_PROVIDER

    env_base_url = (
        os.getenv("NANO_PDF_LOCAL_BASE_URL", "").strip()
        or os.getenv("OPENAI_BASE_URL", "").strip()
        or os.getenv("OLLAMA_HOST", "").strip()
    )
    base_url = _normalize_base_url(env_base_url)
    if not base_url and inferred_provider in LOCAL_PROVIDER_HINTS:
        base_url = DEFAULT_LOCAL_BASE_URL

    return {
        "provider": inferred_provider,
        "model": merged.get("chat.activeModel", "").strip(),
        "base_url": base_url,
        "storage_root": merged.get("chat.localModel.storageRoot", "").strip(),
        "model_path": merged.get("chat.localModel.modelPath", "").strip(),
        "config_sources": sources,
    }


def _parse_provider_config(parsed):
    defaults = _load_blazeclaw_defaults()
    requested_provider = _normalize_provider(_parse_optional_text(parsed, "provider"))
    provider = requested_provider or defaults["provider"] or DEFAULT_PROVIDER

    requested_model = _parse_optional_text(parsed, "model")
    model = requested_model or defaults["model"]
    if provider == DEFAULT_PROVIDER and not model:
        model = DEFAULT_GEMINI_MODEL

    requested_base_url = _normalize_base_url(_parse_optional_text(parsed, "baseUrl"))
    base_url = requested_base_url or ""
    if not base_url and provider in LOCAL_PROVIDER_HINTS:
        base_url = defaults["base_url"] or DEFAULT_LOCAL_BASE_URL

    api_key_env_name = _parse_optional_text(parsed, "apiKeyEnvName")
    if not api_key_env_name:
        api_key_env_name = (
            DEFAULT_GEMINI_API_KEY_ENV
            if provider == DEFAULT_PROVIDER
            else DEFAULT_OPENAI_API_KEY_ENV
        )

    theme = _parse_optional_text(parsed, "theme").lower()
    if theme and theme not in THEME_PRESETS:
        raise ValueError(f"theme must be one of: {', '.join(sorted(THEME_PRESETS.keys()))}")

    return {
        "provider": provider,
        "model": model,
        "base_url": base_url,
        "api_key_env_name": api_key_env_name,
        "preflight_only": _parse_optional_bool(parsed, "preflightOnly"),
        "theme": theme,
        "config_defaults": defaults,
    }


def _parse_args(argv):
    if len(argv) < 2:
        raise ValueError("Usage: python nano_pdf_bridge.py '<JSON>'")

    raw = argv[1]
    if isinstance(raw, str) and raw.startswith("b64:"):
        encoded = raw[4:]
        try:
            decoded = base64.b64decode(encoded).decode("utf-8")
        except Exception as exc:
            raise ValueError(f"JSON parse error: invalid base64 payload ({exc})") from exc
        try:
            parsed = json.loads(decoded)
        except json.JSONDecodeError as exc:
            raise ValueError(f"JSON parse error: {exc}") from exc
    else:
        try:
            parsed = json.loads(raw)
        except json.JSONDecodeError:
            if len(argv) > 2:
                merged = " ".join(argv[1:])
                try:
                    parsed = json.loads(merged)
                except json.JSONDecodeError as exc:
                    raise ValueError(f"JSON parse error: {exc}") from exc
            else:
                raise

    if not isinstance(parsed, dict):
        raise ValueError("request body must be a JSON object")

    tool_id = parsed.get("toolId", "nano_pdf.edit")
    if tool_id not in ("nano_pdf.edit", "nano_pdf.generate"):
        raise ValueError("unsupported toolId for nano-pdf bridge")

    if tool_id == "nano_pdf.generate":
        content = parsed.get("content")
        if not isinstance(content, str) or not content.strip():
            raise ValueError("content is required")
        content = content.strip()

        output_path = _safe_resolve_path(parsed.get("outputPath", ""))
        if output_path is None:
            raise ValueError("outputPath is required")
        if output_path.suffix.lower() != ".pdf":
            raise ValueError("outputPath must target a .pdf file")
        output_path.parent.mkdir(parents=True, exist_ok=True)

        title = parsed.get("title", "")
        if title and not isinstance(title, str):
            raise ValueError("title must be a string")

        return {
            "tool_id": tool_id,
            "content": content,
            "output_path": output_path,
            "title": title.strip() if isinstance(title, str) else "",
        }

    input_path = _safe_resolve_path(parsed.get("inputPath", ""))
    if input_path is None:
        output_hint = _safe_resolve_path(parsed.get("outputPath", ""))
        if output_hint is not None:
            draft_hint = output_hint.with_name(f"{output_hint.stem}_draft.pdf")
            raise ValueError(
                "inputPath is required for nano_pdf.edit. "
                f"First call nano_pdf.generate to create a draft, e.g. outputPath='{draft_hint}', then call nano_pdf.edit with inputPath set to that draft."
            )
        raise ValueError(
            "inputPath is required; nano_pdf.edit only edits an existing PDF. "
            "Create a draft PDF first and pass it via inputPath"
        )

    if input_path.suffix.lower() != ".pdf":
        raise ValueError("inputPath must target a .pdf file")

    if not input_path.exists() or not input_path.is_file():
        raise ValueError("inputPath does not exist or is not a file")

    page_index = parsed.get("pageIndex", 0)
    if not isinstance(page_index, int) or page_index < 0:
        raise ValueError("pageIndex must be an integer >= 0")

    instruction = parsed.get("instruction")
    if not isinstance(instruction, str) or not instruction.strip():
        instruction = "Apply a professional business brief layout with clear sections, executive-summary emphasis, and polished typography."
    instruction = instruction.strip()

    output_candidate = parsed.get("outputPath", "")
    if output_candidate:
        output_path = _safe_resolve_path(output_candidate)
        if output_path is None:
            raise ValueError("outputPath is invalid")
    else:
        output_path = _derive_output_path(input_path)

    if output_path == input_path:
        raise ValueError("outputPath must differ from inputPath")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    provider_config = _parse_provider_config(parsed)

    return {
        "tool_id": tool_id,
        "input_path": input_path,
        "page_index": page_index,
        "instruction": instruction,
        "output_path": output_path,
        **provider_config,
    }


def _run_generate(args):
    try:
        payload = _build_simple_text_pdf(args["content"], args.get("title", ""))
        args["output_path"].write_bytes(payload)
    except Exception as exc:
        return _error("execution_failed", "failed to generate draft PDF", str(exc))

    return _success(
        outputPath=str(args["output_path"]),
        mode="generated",
    )


def _build_auth_headers(args):
    headers = {
        "Content-Type": "application/json",
    }
    api_key_env_name = args.get("api_key_env_name", "")
    if api_key_env_name:
        api_key = os.getenv(api_key_env_name, "")
        if api_key:
            headers["Authorization"] = f"Bearer {api_key}"
    return headers


def _json_request(url: str, payload=None, headers=None, method: str = "POST", timeout: int = 8):
    request_headers = headers or {}
    data = None
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(url, data=data, headers=request_headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            body = response.read().decode("utf-8", errors="replace")
            return response.status, body
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"HTTP {exc.code}: {body.strip()}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(str(exc.reason)) from exc


def _unique_preserve_order(values):
    result = []
    seen = set()
    for value in values:
        if not value or value in seen:
            continue
        seen.add(value)
        result.append(value)
    return result


def _candidate_openai_model_urls(base_url: str):
    candidates = []
    if base_url.endswith("/v1"):
        candidates.append(f"{base_url}/models")
    else:
        candidates.append(f"{base_url}/v1/models")
        candidates.append(f"{base_url}/models")
    return _unique_preserve_order(candidates)


def _candidate_openai_chat_urls(base_url: str):
    candidates = []
    if base_url.endswith("/v1"):
        candidates.append(f"{base_url}/chat/completions")
    else:
        candidates.append(f"{base_url}/v1/chat/completions")
        candidates.append(f"{base_url}/chat/completions")
    return _unique_preserve_order(candidates)


def _probe_openai_backend(base_url: str, headers):
    errors = []
    for url in _candidate_openai_model_urls(base_url):
        try:
            _, body = _json_request(url, payload=None, headers=headers, method="GET")
            payload = json.loads(body) if body else {}
            models = [
                item.get("id", "")
                for item in payload.get("data", [])
                if isinstance(item, dict)
            ]
            return {
                "backend": "openai-compatible",
                "probe_url": url,
                "models": [model for model in models if model],
            }
        except Exception as exc:
            errors.append(f"{url}: {exc}")
    raise RuntimeError("; ".join(errors) or "openai-compatible probe failed")


def _probe_ollama_backend(base_url: str, headers):
    url = f"{base_url}/api/tags"
    _, body = _json_request(url, payload=None, headers=headers, method="GET")
    payload = json.loads(body) if body else {}
    models = []
    for item in payload.get("models", []):
        if isinstance(item, dict):
            name = item.get("name") or item.get("model") or ""
            if name:
                models.append(name)
    return {
        "backend": "ollama",
        "probe_url": url,
        "models": models,
    }


def _normalize_model_key(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", value.lower()) if value else ""


def _model_candidates(model: str):
    raw = (model or "").strip()
    if not raw:
        return []
    candidates = [raw]
    if "/" in raw:
        candidates.append(raw.split("/", 1)[1])
    if ":" in raw:
        candidates.append(raw.split(":", 1)[0])
    return _unique_preserve_order(candidates)


def _resolve_local_model(model: str, available_models):
    candidates = _model_candidates(model)
    if not available_models:
        return candidates[0] if candidates else ""

    available_lookup = {}
    for available in available_models:
        keys = {
            available,
            available.split(":", 1)[0],
            _normalize_model_key(available),
            _normalize_model_key(available.split(":", 1)[0]),
        }
        for key in keys:
            if key:
                available_lookup.setdefault(key, available)

    for candidate in candidates:
        lookup_keys = [candidate, candidate.split(":", 1)[0], _normalize_model_key(candidate)]
        for key in lookup_keys:
            if key in available_lookup:
                return available_lookup[key]
    return ""


def _extract_text_from_pdf(input_path: Path) -> str:
    raw = input_path.read_bytes()
    matches = PDF_TEXT_PATTERN.findall(raw)
    lines = []
    for match in matches:
        text = match.decode("latin-1", errors="replace")
        text = text.replace(r"\(", "(").replace(r"\)", ")").replace(r"\\", "\\")
        text = text.strip()
        if text and text not in lines:
            lines.append(text)
    if not lines:
        decoded = raw.decode("latin-1", errors="replace")
        snippets = re.findall(r"[A-Za-z0-9][A-Za-z0-9 ,.:;()/%+-]{8,}", decoded)
        for snippet in snippets[:40]:
            value = snippet.strip()
            if value and value not in lines:
                lines.append(value)
    return "\n".join(lines).strip()


def _local_backend_priority(provider: str):
    return LOCAL_BACKEND_PRIORITY.get(provider, ("openai-compatible", "ollama"))


def _build_preflight(args):
    provider = args["provider"]
    model = args["model"]
    base_url = args["base_url"]
    defaults = args.get("config_defaults", {})

    if provider == DEFAULT_PROVIDER:
        if model and model != DEFAULT_GEMINI_MODEL:
            return {
                "ok": False,
                "errorCode": "unsupported_model",
                "message": f"nano-pdf model '{model}' is not supported by the installed CLI",
                "details": f"The installed nano-pdf package hard-codes '{DEFAULT_GEMINI_MODEL}' for edit operations.",
            }
        if base_url:
            return {
                "ok": False,
                "errorCode": "unsupported_base_url",
                "message": "nano-pdf baseUrl override is not supported by the installed Gemini CLI path",
                "details": "Use a local provider to route through the BlazeClaw fallback path, or omit baseUrl for Gemini-backed CLI execution.",
            }
        if not os.getenv(args["api_key_env_name"], ""):
            return {
                "ok": False,
                "errorCode": "provider_not_configured",
                "message": f"{args['api_key_env_name']} is required for Gemini-backed nano-pdf editing",
                "details": "The installed nano-pdf package only supports Gemini edit mode. Configure the requested API key environment variable or use the local fallback path.",
            }
        return {
            "ok": True,
            "route": "gemini-cli",
            "provider": provider,
            "resolvedModel": DEFAULT_GEMINI_MODEL,
            "apiKeyEnvName": args["api_key_env_name"],
            "configSources": defaults.get("config_sources", []),
        }

    if provider not in LOCAL_PROVIDER_HINTS:
        return {
            "ok": False,
            "errorCode": "unsupported_provider",
            "message": f"nano-pdf provider '{provider}' is not supported",
            "details": "Supported providers are gemini, local, llama.cpp, ollama, and openai-compatible.",
        }

    if not model:
        return {
            "ok": False,
            "errorCode": "provider_not_configured",
            "message": "No local model is configured for nano-pdf fallback execution",
            "details": "Set model explicitly or ensure BlazeClaw config defines chat.activeModel for the active local provider.",
        }

    if not base_url:
        return {
            "ok": False,
            "errorCode": "provider_not_configured",
            "message": "No local baseUrl is configured for nano-pdf fallback execution",
            "details": "Set baseUrl explicitly, set NANO_PDF_LOCAL_BASE_URL, OPENAI_BASE_URL, or OLLAMA_HOST, or expose a compatible local HTTP endpoint.",
        }

    headers = _build_auth_headers(args)
    probe_errors = []
    for backend in _local_backend_priority(provider):
        try:
            if backend == "openai-compatible":
                probe = _probe_openai_backend(base_url, headers)
            else:
                probe = _probe_ollama_backend(base_url, headers)
        except Exception as exc:
            probe_errors.append(f"{backend}: {exc}")
            continue

        resolved_model = _resolve_local_model(model, probe["models"])
        if probe["models"] and not resolved_model:
            return {
                "ok": False,
                "errorCode": "local_model_not_available",
                "message": f"Local model '{model}' is not available on {backend}",
                "details": f"Available models: {', '.join(probe['models'][:12])}",
            }
        if not resolved_model:
            resolved_model = _model_candidates(model)[0]

        return {
            "ok": True,
            "route": "local-fallback",
            "provider": provider,
            "resolvedModel": resolved_model,
            "requestedModel": model,
            "baseUrl": base_url,
            "backend": probe["backend"],
            "probeUrl": probe["probe_url"],
            "availableModels": probe["models"][:20],
            "configSources": defaults.get("config_sources", []),
        }

    return {
        "ok": False,
        "errorCode": "local_model_unreachable",
        "message": f"Unable to reach local provider '{provider}' for nano-pdf fallback execution",
        "details": "; ".join(probe_errors) or f"No compatible local endpoint responded at {base_url}",
    }


def _build_local_edit_prompt(args, draft_text: str) -> str:
    source_text = draft_text.strip() or "(no source text could be extracted from the draft PDF)"
    return (
        "You are preparing a polished PDF-ready business brief. "
        "Rewrite the source material into plain text only, with a strong title, an executive summary, "
        "3-5 key findings, implications, and a concise conclusion. "
        "Do not return markdown fences, JSON, or commentary. Keep the result under 60 lines.\n\n"
        f"Editing instruction:\n{args['instruction']}\n\n"
        f"Source draft text:\n{source_text}\n"
    )


def _invoke_openai_completion(base_url: str, model: str, prompt: str, headers):
    payload = {
        "model": model,
        "messages": [
            {
                "role": "system",
                "content": "Return only polished report text for a PDF document.",
            },
            {
                "role": "user",
                "content": prompt,
            },
        ],
        "temperature": 0.2,
        "max_tokens": 900,
    }
    last_error = None
    for url in _candidate_openai_chat_urls(base_url):
        try:
            _, body = _json_request(url, payload=payload, headers=headers, method="POST")
            response = json.loads(body) if body else {}
            choices = response.get("choices", [])
            if not choices:
                raise RuntimeError("OpenAI-compatible endpoint returned no choices")
            message = choices[0].get("message", {})
            content = message.get("content", "")
            if isinstance(content, list):
                content = "\n".join(
                    item.get("text", "") for item in content if isinstance(item, dict)
                )
            content = str(content).strip()
            if not content:
                raise RuntimeError("OpenAI-compatible endpoint returned empty content")
            return content
        except Exception as exc:
            last_error = exc
    raise RuntimeError(str(last_error) if last_error else "OpenAI-compatible generation failed")


def _invoke_ollama_completion(base_url: str, model: str, prompt: str, headers):
    url = f"{base_url}/api/generate"
    payload = {
        "model": model,
        "prompt": prompt,
        "stream": False,
        "options": {
            "temperature": 0.2,
            "num_predict": 900,
        },
    }
    _, body = _json_request(url, payload=payload, headers=headers, method="POST")
    response = json.loads(body) if body else {}
    content = str(response.get("response", "")).strip()
    if not content:
        raise RuntimeError("Ollama endpoint returned empty response")
    return content


def _run_local_fallback(args, preflight):
    draft_text = _extract_text_from_pdf(args["input_path"])
    prompt = _build_local_edit_prompt(args, draft_text)
    headers = _build_auth_headers(args)

    try:
        if preflight["backend"] == "openai-compatible":
            report_text = _invoke_openai_completion(
                preflight["baseUrl"],
                preflight["resolvedModel"],
                prompt,
                headers,
            )
        else:
            report_text = _invoke_ollama_completion(
                preflight["baseUrl"],
                preflight["resolvedModel"],
                prompt,
                headers,
            )
    except Exception as exc:
        return _error(
            "execution_failed",
            "local nano-pdf fallback generation failed",
            str(exc),
        )

    try:
        output_bytes = _build_report_pdf(
            report_text,
            title="Battery Report",
            requested_theme=args.get("theme", ""),
        )
        args["output_path"].write_bytes(output_bytes)
    except Exception as exc:
        return _error(
            "execution_failed",
            "failed to render fallback PDF output",
            str(exc),
        )

    return _success(
        outputPath=str(args["output_path"]),
        pageIndex=args["page_index"],
        instruction=args["instruction"],
        provider=preflight["provider"],
        model=preflight["resolvedModel"],
        requestedModel=preflight.get("requestedModel", preflight["resolvedModel"]),
        baseUrl=preflight["baseUrl"],
        backend=preflight["backend"],
        mode="local-fallback",
        fallbackMode="whole-document-rerender",
        sourceTextExtracted=bool(draft_text),
    )


def _build_cli_environment(args):
    env = os.environ.copy()
    api_key_env_name = args["api_key_env_name"]
    api_key = env.get(api_key_env_name, "")
    if api_key_env_name != DEFAULT_GEMINI_API_KEY_ENV and api_key:
        env[DEFAULT_GEMINI_API_KEY_ENV] = api_key
    return env


def _run_gemini_cli(args, preflight):
    exe = shutil.which("nano-pdf")
    if not exe:
        return _error(
            "missing_dependency",
            "nano-pdf executable not found on PATH",
            "Install with: uv pip install nano-pdf",
        )

    cmd = [
        exe,
        "edit",
        str(args["input_path"]),
        str(args["page_index"]),
        args["instruction"],
        "--output",
        str(args["output_path"]),
    ]
    process_env = _build_cli_environment(args)

    def _invoke(command):
        return subprocess.run(
            command,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
            env=process_env,
        )

    try:
        completed = _invoke(cmd)
    except Exception as exc:
        return _error("execution_failed", "failed to invoke nano-pdf", str(exc))

    if completed.returncode != 0 and args["page_index"] == 0:
        retry_cmd = cmd.copy()
        retry_cmd[3] = "1"
        retry_completed = _invoke(retry_cmd)
        if retry_completed.returncode == 0:
            completed = retry_completed

    if completed.returncode != 0:
        details = (completed.stderr or completed.stdout or "").strip()
        return _error(
            "execution_failed",
            f"nano-pdf exited with code {completed.returncode}",
            details,
        )

    payload = {
        "outputPath": str(args["output_path"]),
        "pageIndex": args["page_index"],
        "instruction": args["instruction"],
        "provider": preflight["provider"],
        "model": preflight["resolvedModel"],
        "apiKeyEnvName": preflight["apiKeyEnvName"],
        "mode": "gemini-cli",
    }
    stdout = (completed.stdout or "").strip()
    if stdout:
        payload["cliOutput"] = stdout
    return _success(**payload)


def _run_cli(args):
    if args.get("tool_id") == "nano_pdf.generate":
        return _run_generate(args)

    preflight = _build_preflight(args)
    if not preflight["ok"]:
        return _error(
            preflight["errorCode"],
            preflight["message"],
            preflight.get("details", ""),
        )

    if args["preflight_only"]:
        return _success(
            mode="preflight",
            provider=preflight["provider"],
            requestedModel=preflight.get("requestedModel", args["model"]),
            resolvedModel=preflight["resolvedModel"],
            baseUrl=preflight.get("baseUrl", ""),
            backend=preflight.get("backend", ""),
            route=preflight["route"],
            probeUrl=preflight.get("probeUrl", ""),
            configSources=preflight.get("configSources", []),
            availableModels=preflight.get("availableModels", []),
            apiKeyEnvName=preflight.get("apiKeyEnvName", ""),
        )

    if preflight["route"] == "local-fallback":
        return _run_local_fallback(args, preflight)

    return _run_gemini_cli(args, preflight)


def main(argv):
    try:
        args = _parse_args(argv)
    except ValueError as exc:
        return _error("invalid_args", str(exc))

    return _run_cli(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
