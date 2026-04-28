import json
import runpy
import sys
from pathlib import Path


def _resolve_original_bridge() -> Path | None:
    current = Path(__file__).resolve()
    candidates = []

    for parent in [current.parent, *current.parents]:
        candidates.append(parent / "blazeclaw" / "skills-openclaw-original" / "nano-pdf" / "scripts" / "nano_pdf_bridge.py")
        candidates.append(parent / "skills-openclaw-original" / "nano-pdf" / "scripts" / "nano_pdf_bridge.py")

    for candidate in candidates:
        if candidate.exists() and candidate.resolve() != current:
            return candidate

    return None


def _emit_error(code: str, message: str) -> int:
    print(json.dumps({
        "ok": False,
        "errorCode": code,
        "message": message,
    }, ensure_ascii=False, indent=2))
    return 1


def main() -> int:
    target = _resolve_original_bridge()
    if target is None:
        return _emit_error(
            "missing_dependency",
            "Bundled nano-pdf delegating bridge could not locate skills-openclaw-original runtime bridge."
        )

    original_argv0 = sys.argv[0] if sys.argv else ""
    try:
        if sys.argv:
            sys.argv[0] = str(target)
        runpy.run_path(str(target), run_name="__main__")
        return 0
    except SystemExit as ex:
        code = ex.code
        if isinstance(code, int):
            return code
        return 0 if code is None else 1
    finally:
        if sys.argv:
            sys.argv[0] = original_argv0


if __name__ == "__main__":
    raise SystemExit(main())
