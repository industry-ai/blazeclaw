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

    input_path = _safe_resolve_path(parsed.get("inputPath", ""))
    if input_path is None:
        raise ValueError("inputPath is required")

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
        "input_path": input_path,
        "page_index": page_index,
        "instruction": instruction,
        "output_path": output_path,
    }


def _run_cli(args):
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

    try:
        completed = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    except Exception as exc:
        return _error("execution_failed", "failed to invoke nano-pdf", str(exc))

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
