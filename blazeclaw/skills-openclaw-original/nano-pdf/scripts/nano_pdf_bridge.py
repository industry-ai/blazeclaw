import json
import shutil
import subprocess
import sys
from pathlib import Path


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


def _derive_output_path(input_path: Path) -> Path:
    if input_path.suffix.lower() == ".pdf":
        return input_path.with_name(f"{input_path.stem}.edited.pdf")
    return input_path.with_name(f"{input_path.name}.edited.pdf")


def _escape_pdf_text(value: str) -> str:
    return value.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")


def _build_simple_text_pdf(content: str, title: str = "") -> bytes:
    safe_title = _escape_pdf_text(title.strip()) if title else "Generated Report"
    lines = [line.rstrip() for line in content.splitlines()]
    if not lines:
        lines = [content.strip()]
    lines = [line for line in lines if line]
    if not lines:
        lines = ["(empty report)"]

    lines = lines[:60]
    line_ops = []
    y = 760
    for idx, line in enumerate(lines):
        escaped = _escape_pdf_text(line[:100])
        if idx == 0:
            line_ops.append(f"BT /F1 16 Tf 72 {y} Td ({escaped}) Tj ET")
        else:
            y -= 14
            if y < 72:
                break
            line_ops.append(f"BT /F1 11 Tf 72 {y} Td ({escaped}) Tj ET")

    if title.strip():
        line_ops.insert(0, f"BT /F1 10 Tf 72 792 Td ({safe_title}) Tj ET")

    stream = "\n".join(line_ops).encode("latin-1", errors="replace")

    objects = []
    objects.append(b"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n")
    objects.append(b"2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n")
    objects.append(
        b"3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
        b"/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>\nendobj\n"
    )
    objects.append(
        b"4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n"
    )
    objects.append(
        b"5 0 obj\n<< /Length " + str(len(stream)).encode("ascii") + b" >>\nstream\n" +
        stream + b"\nendstream\nendobj\n"
    )

    header = b"%PDF-1.4\n"
    body = bytearray(header)
    xref_positions = [0]
    for obj in objects:
        xref_positions.append(len(body))
        body.extend(obj)

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


def _safe_resolve_path(raw: str):
    if not isinstance(raw, str) or not raw.strip():
        return None
    value = raw.strip()
    if "\x00" in value:
        return None
    return Path(value).expanduser().resolve()


def _parse_args(argv):
    if len(argv) < 2:
        raise ValueError("Usage: python nano_pdf_bridge.py '<JSON>'")

    try:
        parsed = json.loads(argv[1])
    except json.JSONDecodeError as exc:
        raise ValueError(f"JSON parse error: {exc}") from exc

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
        raise ValueError(
            "inputPath is required; nano_pdf.edit only edits an existing PDF. "
            "Create a draft PDF first and pass it via inputPath"
        )

    if input_path.suffix.lower() != ".pdf":
        raise ValueError("inputPath must target a .pdf file")

    if not input_path.exists() or not input_path.is_file():
        raise ValueError("inputPath does not exist or is not a file")

    page_index = parsed.get("pageIndex")
    if not isinstance(page_index, int) or page_index < 0:
        raise ValueError("pageIndex must be an integer >= 0")

    instruction = parsed.get("instruction")
    if not isinstance(instruction, str) or not instruction.strip():
        raise ValueError("instruction is required")
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

    return {
        "tool_id": tool_id,
        "input_path": input_path,
        "page_index": page_index,
        "instruction": instruction,
        "output_path": output_path,
    }


def _run_generate(args):
    try:
        payload = _build_simple_text_pdf(args["content"], args.get("title", ""))
        args["output_path"].write_bytes(payload)
    except Exception as exc:
        return _error("execution_failed", "failed to generate draft PDF", str(exc))

    print(
        json.dumps(
            {
                "ok": True,
                "outputPath": str(args["output_path"]),
                "mode": "generated",
            },
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


def _run_cli(args):
    if args.get("tool_id") == "nano_pdf.generate":
        return _run_generate(args)

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

    def _invoke(cmd):
        completed = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        return completed

    try:
        completed = _invoke(cmd)
    except Exception as exc:
        return _error("execution_failed", "failed to invoke nano-pdf", str(exc))

    if completed.returncode != 0 and args["page_index"] == 0:
        # Some nano-pdf installations effectively behave as 1-based page indexing.
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
        "ok": True,
        "outputPath": str(args["output_path"]),
        "pageIndex": args["page_index"],
        "instruction": args["instruction"],
    }

    stdout = (completed.stdout or "").strip()
    if stdout:
        payload["cliOutput"] = stdout

    print(json.dumps(payload, ensure_ascii=False, indent=2))
    return 0


def main(argv):
    try:
        args = _parse_args(argv)
    except ValueError as exc:
        return _error("invalid_args", str(exc))

    return _run_cli(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
