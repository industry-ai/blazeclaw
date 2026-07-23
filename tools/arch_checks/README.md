# Architecture fitness checks — tools/arch_checks

This directory contains lightweight tooling to help enforce architecture "fitness" checks described in PROJECT_REVIEW.md.

Implemented so far
- file-size checker: tools/arch_checks/file_size_check.py

Quick start

1. Install the Python dependency:

   pip install pyyaml

2. Run the checker against a list of changed files:

   # pass files as arguments
   python tools/arch_checks/file_size_check.py --files $(git diff --name-only origin/main...HEAD)

   # or provide file list via stdin (one per line)
   git diff --name-only origin/main...HEAD | python tools/arch_checks/file_size_check.py --stdin

Config
- tools/arch_checks/config.yml contains default thresholds, ignore globs, and report paths.
- docs/boundary_map.yml contains runtime boundary definitions (used by the boundary-drift checker).

Exit codes
- 0: no violations
- 1: violations found
- 2: configuration or runtime error (e.g., missing PyYAML)

CI
Include a job step that installs Python and PyYAML, then runs the checker with the changed-file list. Save the JSON report (tools/arch_checks/report.json) as an artifact for triage.
