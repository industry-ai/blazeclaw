#!/usr/bin/env python3
"""Simple file-size checker for architecture fitness.

Reads tools/arch_checks/config.yml (or a specified config) and checks a list
of file paths for per-file size thresholds. Prints a human summary and writes
a JSON report. Exits with 0 on success, 1 on violations, 2 on configuration
or runtime errors.

Usage:
  python tools/arch_checks/file_size_check.py --files file1 file2
  cat changed_files.txt | python tools/arch_checks/file_size_check.py --stdin

This script depends on PyYAML (pip install pyyaml).
"""
from __future__ import annotations

import argparse
import fnmatch
import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, List

try:
    import yaml
except Exception:
    print("ERROR: PyYAML is required. Install with: pip install pyyaml", file=sys.stderr)
    sys.exit(2)


def load_config(path: Path) -> Dict[str, Any]:
    if not path.exists():
        raise FileNotFoundError(f"Config not found: {path}")
    with path.open("r", encoding="utf-8") as f:
        return yaml.safe_load(f) or {}


def normalize_path(p: Path, repo_root: Path) -> str:
    try:
        rel = p.relative_to(repo_root)
    except Exception:
        rel = p
    # Use forward slashes for glob matching
    return str(rel).replace("\\", "/")


def matches_any(path_str: str, patterns: List[str]) -> bool:
    for pat in patterns:
        if fnmatch.fnmatch(path_str, pat):
            return True
    return False


def get_threshold_for(path_str: str, cfg: Dict[str, Any]) -> float:
    overrides = cfg.get("threshold_overrides") or []
    for o in overrides:
        pat = o.get("pattern")
        if not pat:
            continue
        if fnmatch.fnmatch(path_str, pat):
            return float(o.get("file_size_mb", cfg.get("defaults", {}).get("file_size_mb_default", 5)))
    return float(cfg.get("defaults", {}).get("file_size_mb_default", 5))


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description="File-size checker for architecture fitness")
    ap.add_argument("--config", default="tools/arch_checks/config.yml", help="Path to config YAML")
    ap.add_argument("--repo-root", default=".", help="Repository root path")
    ap.add_argument("--files", nargs="*", help="List of files to check")
    ap.add_argument("--stdin", action="store_true", help="Read file paths from stdin (one per line)")
    ap.add_argument("--report-json", help="Override report JSON output path (optional)")
    args = ap.parse_args(argv)

    repo_root = Path(args.repo_root).resolve()
    cfg_path = (repo_root / args.config).resolve() if not Path(args.config).is_absolute() else Path(args.config)
    try:
        cfg = load_config(cfg_path)
    except Exception as e:
        print(f"ERROR: Failed to load config: {e}", file=sys.stderr)
        return 2

    ignore_globs = cfg.get("ignore_globs") or []

    files: List[str] = []
    if args.stdin:
        for line in sys.stdin:
            line = line.strip()
            if line:
                files.append(line)
    if args.files:
        files.extend(args.files)

    if not files:
        print("No files provided. Use --files or --stdin.", file=sys.stderr)
        return 2

    violations: List[Dict[str, Any]] = []
    checked: List[Dict[str, Any]] = []

    for f in sorted(set(files)):
        p = (repo_root / f).resolve() if not Path(f).is_absolute() else Path(f)
        path_str = normalize_path(p, repo_root)
        # Skip ignored patterns
        if matches_any(path_str, ignore_globs):
            continue
        if not p.exists():
            # File may have been deleted in patch; skip silently
            continue
        if p.is_dir():
            continue
        try:
            size_bytes = p.stat().st_size
        except OSError:
            continue
        size_mb = size_bytes / (1024.0 * 1024.0)
        threshold = get_threshold_for(path_str, cfg)
        checked.append({"path": path_str, "size_mb": round(size_mb, 3), "threshold_mb": threshold})
        if size_mb > threshold:
            violations.append({"path": path_str, "size_mb": round(size_mb, 3), "threshold_mb": threshold})

    report = {
        "summary": {"checked_count": len(checked), "violations_count": len(violations)},
        "checked": checked,
        "violations": violations,
    }

    # Write JSON report if configured
    report_path = args.report_json or cfg.get("report", {}).get("json_path")
    if report_path:
        try:
            rp = (repo_root / report_path).resolve() if not Path(report_path).is_absolute() else Path(report_path)
            rp.parent.mkdir(parents=True, exist_ok=True)
            with rp.open("w", encoding="utf-8") as f:
                json.dump(report, f, indent=2)
        except Exception as e:
            print(f"WARNING: Failed to write report to {report_path}: {e}", file=sys.stderr)

    # Human-friendly output
    print(f"Checked files: {len(checked)}")
    if violations:
        print(f"Violations: {len(violations)}")
        for v in violations:
            print(f" - {v['path']}: {v['size_mb']} MB (limit {v['threshold_mb']} MB)")
        return 1
    else:
        print("No file-size violations found.")
        return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
