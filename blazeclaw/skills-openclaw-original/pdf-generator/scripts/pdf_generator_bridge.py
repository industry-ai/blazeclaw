#!/usr/bin/env python3
import json
import os
import sys
from datetime import date
from pathlib import Path


def _emit(payload: dict, exit_code: int = 0) -> None:
    sys.stdout.write(json.dumps(payload, ensure_ascii=False))
    sys.exit(exit_code)


def _looks_like_inline_markdown(value: str) -> bool:
    text = value.strip()
    if not text:
        return False
    if "\n" in text or "\r" in text:
        return True
    lowered = text.lower()
    return (
        text.startswith("#")
        or text.startswith("-")
        or text.startswith("*")
        or text.startswith(">")
        or lowered.startswith("```")
    )


def _resolve_paths(args: dict) -> tuple[Path | None, Path]:
    input_raw = args.get("input_md") or args.get("input")
    output_raw = args.get("output_pdf") or args.get("output")

    if not isinstance(output_raw, str) or not output_raw.strip():
        output_raw = "Battery_Report.pdf"

    cwd = Path.cwd()

    input_path: Path | None = None
    if isinstance(input_raw, str) and input_raw.strip() and not _looks_like_inline_markdown(input_raw):
        input_path = Path(input_raw)
        if not input_path.is_absolute():
            input_path = (cwd / input_path).resolve()

    output_path = Path(output_raw)
    if output_raw.startswith("/tmp/") and os.name == "nt":
        output_path = cwd / output_raw.lstrip("/")
    if not output_path.is_absolute():
        output_path = (cwd / output_path).resolve()

    return input_path, output_path


def _pdf_escape(text: str) -> str:
    escaped = text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")
    return escaped.encode("latin-1", errors="replace").decode("latin-1")


def _build_fallback_pdf_bytes(markdown_text: str, title: str, author: str) -> bytes:
    lines = [title, f"Author: {author} | Date: {date.today().isoformat()}", ""]
    lines.extend(markdown_text.splitlines())

    y = 800
    content_lines = ["BT", "/F1 11 Tf"]
    for raw in lines[:48]:
        content_lines.append(f"1 0 0 1 50 {y} Tm ({_pdf_escape(raw)}) Tj")
        y -= 15
        if y < 50:
            break
    content_lines.append("ET")
    stream = "\n".join(content_lines) + "\n"
    stream_bytes = stream.encode("latin-1", errors="replace")

    objects = [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        f"<< /Length {len(stream_bytes)} >>\nstream\n{stream}endstream",
    ]

    pdf_parts: list[bytes] = [b"%PDF-1.4\n"]
    offsets = [0]
    for index, obj in enumerate(objects, start=1):
        offsets.append(sum(len(part) for part in pdf_parts))
        pdf_parts.append(f"{index} 0 obj\n{obj}\nendobj\n".encode("latin-1", errors="replace"))

    xref_offset = sum(len(part) for part in pdf_parts)
    xref = [f"xref\n0 {len(objects) + 1}\n", "0000000000 65535 f \n"]
    for offset in offsets[1:]:
        xref.append(f"{offset:010d} 00000 n \n")

    trailer = (
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
        f"startxref\n{xref_offset}\n%%EOF\n"
    )

    pdf_parts.append("".join(xref).encode("latin-1"))
    pdf_parts.append(trailer.encode("latin-1"))
    return b"".join(pdf_parts)


def _build_pdf_bytes(markdown_text: str, title: str, author: str) -> bytes:
    escaped = (
        markdown_text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
    )

    lines = [line.rstrip() for line in escaped.splitlines()]
    body = "\n".join(line if line else "<br/>" for line in lines)

    try:
        from reportlab.lib.pagesizes import A4
        from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
        from reportlab.lib.units import cm
        from reportlab.platypus import Paragraph, SimpleDocTemplate, Spacer
    except ModuleNotFoundError:
        return _build_fallback_pdf_bytes(markdown_text, title, author)

    styles = getSampleStyleSheet()
    title_style = ParagraphStyle(
        "BlazePdfTitle",
        parent=styles["Heading1"],
        fontSize=18,
        leading=22,
        spaceAfter=12,
    )
    meta_style = ParagraphStyle(
        "BlazePdfMeta",
        parent=styles["Normal"],
        fontSize=10,
        textColor="#666666",
        spaceAfter=16,
    )
    body_style = ParagraphStyle(
        "BlazePdfBody",
        parent=styles["Normal"],
        fontSize=11,
        leading=16,
    )

    from io import BytesIO

    buffer = BytesIO()
    doc = SimpleDocTemplate(
        buffer,
        pagesize=A4,
        leftMargin=2 * cm,
        rightMargin=2 * cm,
        topMargin=2 * cm,
        bottomMargin=2 * cm,
        title=title,
        author=author,
    )

    story = [
        Paragraph(title, title_style),
        Paragraph(f"Author: {author} | Date: {date.today().isoformat()}", meta_style),
        Spacer(1, 6),
        Paragraph(body, body_style),
    ]

    doc.build(story)
    return buffer.getvalue()


def main() -> None:
    try:
        raw = os.environ.get("BLAZECLAW_TOOL_ARGS_JSON", "")
        if not raw:
            raw = sys.argv[1] if len(sys.argv) > 1 else "{}"

        args = json.loads(raw) if raw else {}
        if not isinstance(args, dict):
            raise ValueError("Tool args must be a JSON object")

        input_path, output_path = _resolve_paths(args)

        title = str(args.get("title") or "Untitled Report")
        author = str(args.get("author") or "BlazeClaw")

        markdown_text = ""
        if input_path is not None:
            if not input_path.is_file():
                inline_fallback = str(args.get("input_md") or args.get("input") or "").strip()
                if _looks_like_inline_markdown(inline_fallback):
                    markdown_text = inline_fallback
                else:
                    raise FileNotFoundError(f"Input markdown not found: {input_path}")
            else:
                markdown_text = input_path.read_text(encoding="utf-8")
        else:
            markdown_text = str(
                args.get("markdown")
                or args.get("text")
                or args.get("input_md")
                or args.get("input")
                or ""
            ).strip()
            if not markdown_text:
                markdown_text = "# Business Brief\n\nNo markdown input provided by caller."

        pdf_bytes = _build_pdf_bytes(markdown_text, title, author)

        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_bytes(pdf_bytes)

        _emit(
            {
                "ok": True,
                "tool": "pdf_generator.generate",
                "input": str(input_path) if input_path is not None else "(inline_markdown)",
                "output": str(output_path),
                "bytes": len(pdf_bytes),
                "title": title,
                "author": author,
            }
        )
    except Exception as ex:
        _emit(
            {
                "ok": False,
                "tool": "pdf_generator.generate",
                "error": {
                    "code": type(ex).__name__,
                    "message": str(ex),
                },
            },
            exit_code=1,
        )


if __name__ == "__main__":
    main()
