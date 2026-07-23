#!/usr/bin/env python3
"""Boundary-drift checker.

Reads docs/boundary_map.yml and tools/arch_checks/config.yml (overrides) and
validates a list of changed files are within declared runtime boundaries.

Usage:
  python tools/arch_checks/boundary_drift_check.py --files file1 file2
  git diff --name-only origin/main...HEAD | python tools/arch_checks/boundary_drift_check.py --stdin

Exits:
  0 = no violations
  1 = violations found
  2 = config/runtime error
"""
from __future__ import annotations

import argparse
import fnmatch
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

try:
    import yaml
except Exception:
    print("ERROR: PyYAML is required. Install with: pip install pyyaml", file=sys.stderr)
    sys.exit(2)


def load_yaml(path: Path) -> Dict[str, Any]:
    if not path.exists():
        raise FileNotFoundError(f"YAML not found: {path}")
    with path.open("r", encoding="utf-8") as f:
        return yaml.safe_load(f) or {}


def normalize_path(p: Path, repo_root: Path) -> str:
    try:
        rel = p.relative_to(repo_root)
    except Exception:
        rel = p
    return str(rel).replace("\\", "/")


def matches_any(path_str: str, patterns: List[str]) -> bool:
    for pat in patterns:
        if fnmatch.fnmatch(path_str, pat):
            return True
    return False


def find_matching_boundary(path_str: str, boundaries: List[Dict[str, Any]]) -> Optional[Dict[str, Any]]:
    for b in boundaries:
        allowed = b.get("allowed_paths") or []
        for pat in allowed:
            if fnmatch.fnmatch(path_str, pat):
                return b
    return None


def main(argv: List[str]) -> int:
    ap = argparse.ArgumentParser(description="Boundary-drift checker")
    ap.add_argument("--config", default="tools/arch_checks/config.yml", help="Path to config YAML")
    ap.add_argument("--boundary-map", default="docs/boundary_map.yml", help="Path to boundary map YAML")
    ap.add_argument("--repo-root", default=".", help="Repository root path")
    ap.add_argument("--files", nargs="*", help="List of files to check")
    ap.add_argument("--stdin", action="store_true", help="Read file paths from stdin (one per line)")
    ap.add_argument("--report-json", help="Override report JSON output path (optional)")
    args = ap.parse_args(argv)

    repo_root = Path(args.repo_root).resolve()
    cfg_path = (repo_root / args.config).resolve() if not Path(args.config).is_absolute() else Path(args.config)
    bm_path = (repo_root / args.boundary_map).resolve() if not Path(args.boundary_map).is_absolute() else Path(args.boundary_map)

    try:
        cfg = load_yaml(cfg_path)
        bm = load_yaml(bm_path)
    except Exception as e:
        print(f"ERROR: Failed to load YAML: {e}", file=sys.stderr)
        return 2

    ignore_globs = list(cfg.get("ignore_globs") or [])
    # include boundary_map global ignores if present
    global_ignores = bm.get("global_ignores") or []
    for g in global_ignores:
        if g not in ignore_globs:
            ignore_globs.append(g)

    boundaries = bm.get("boundaries") or []

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
        # skip ignored patterns
        if matches_any(path_str, ignore_globs):
            continue
        # if file doesn't exist (deleted), skip
        if not p.exists():
            continue
        if p.is_dir():
            continue

        matched = find_matching_boundary(path_str, boundaries)
        checked.append({"path": path_str, "matched_boundary": matched.get("id") if matched else None})
        if not matched:
            violations.append({"path": path_str, "reason": "no matching boundary", "suggested_owners": [b.get("owner") for b in boundaries]})

    report = {"summary": {"checked_count": len(checked), "violations_count": len(violations)}, "checked": checked, "violations": violations}

    report_path = args.report_json or cfg.get("report", {}).get("json_path")
    if report_path:
        try:
            rp = (repo_root / report_path).resolve() if not Path(report_path).is_absolute() else Path(report_path)
            rp.parent.mkdir(parents=True, exist_ok=True)
            with rp.open("w", encoding="utf-8") as f:
                json.dump(report, f, indent=2)
        except Exception as e:
            print(f"WARNING: Failed to write report to {report_path}: {e}", file=sys.stderr)

    print(f"Checked files: {len(checked)}")
    if violations:
        print(f"Violations: {len(violations)}")
        for v in violations:
            owners = v.get("suggested_owners") or []
            owners_str = ", ".join([o for o in owners if o])
            print(f" - {v['path']}: {v['reason']} (suggested owners: {owners_str})")
        return 1
    else:
        print("No boundary-drift violations found.")
        return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
