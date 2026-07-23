# Architecture fitness checks — tools/arch_checks

This directory contains lightweight tooling to help enforce architecture "fitness" checks described in PROJECT_REVIEW.md.

Implemented so far
- file-size checker: tools/arch_checks/file_size_check.py
 - boundary-drift checker: tools/arch_checks/boundary_drift_check.py

Quick start

1. Install the Python dependency:

   pip install pyyaml

2. Run the checker against a list of changed files:

   # pass files as arguments
   python tools/arch_checks/file_size_check.py --files $(git diff --name-only origin/main...HEAD)

   # or provide file list via stdin (one per line)
   git diff --name-only origin/main...HEAD | python tools/arch_checks/file_size_check.py --stdin

Boundary drift checker

- Validate that changed files remain inside declared runtime boundaries:

  git diff --name-only origin/main...HEAD | python tools/arch_checks/boundary_drift_check.py --stdin

Notes
- Both checkers respect ignore patterns in tools/arch_checks/config.yml and docs/boundary_map.yml global_ignores.
- Both tools emit a JSON report to the path configured in tools/arch_checks/config.yml (report.json by default).

Ignore patterns file

- You can centralize ignore patterns in tools/arch_checks/ignore_patterns.txt and reference it from tools/arch_checks/config.yml using the `ignore_file` setting. Patterns are gitignore-style, one per line. Use `#` for comments.

Example entry in config.yml:

  ignore_file: "tools/arch_checks/ignore_patterns.txt"

This file is loaded automatically by both checkers when present.

Config
- tools/arch_checks/config.yml contains default thresholds, ignore globs, and report paths.
- docs/boundary_map.yml contains runtime boundary definitions (used by the boundary-drift checker).

Exit codes
- 0: no violations
- 1: violations found
- 2: configuration or runtime error (e.g., missing PyYAML)

CI
Include a job step that installs Python and PyYAML, then runs the checker with the changed-file list. Save the JSON report (tools/arch_checks/report.json) as an artifact for triage.

Runner script

Use the PowerShell runner to execute both checks and produce an aggregated report:

  powershell -ExecutionPolicy Bypass -File tools/arch_checks/run_arch_checks.ps1 -RepoRoot . -Base origin/main -Mode changed

The runner writes per-check JSON reports and a combined report at tools/arch_checks/arch_checks_report.json by default.
