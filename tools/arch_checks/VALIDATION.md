Validation run (sample)
=======================

Run date: 2026-07-23 (sample)

Command executed (scans all tracked files):

  powershell -NoProfile -ExecutionPolicy Bypass -File tools/arch_checks/run_arch_checks.ps1 -RepoRoot . -Base origin/main -Mode all

Environment (sample):
- Python 3.14.0
- PyYAML 6.0.3

Summary results
- file-size checker: checked 2627 files, violations 3
- boundary-drift checker: checked 2627 files, violations 2627

Top file-size violations
- blazeclaw/BlazeClawMfc/models/chat/Qwen3-0.6B-ONNX/tokenizer.json — 8.695 MB (limit 5 MB)
- blazeclaw/BlazeClawMfc/models/chat/Qwen3-14B-ONNX/tokenizer.json — 10.893 MB (limit 5 MB)
- blazeclaw/skills-openclaw-original/h5-cards/assets/video/activity1.mp4 — 9.563 MB (limit 5 MB)

Interpretation and next actions
- The file-size checker correctly detected large artifacts that may be problematic to keep in the repo. Consider moving large model artifacts and media into releases or large-file storage (Git LFS) and adding appropriate threshold overrides in tools/arch_checks/config.yml (see threshold_overrides).
- The boundary-drift checker reported many violations because the default boundary map is intentionally conservative. To reduce noise:
  - Add more allowed_paths entries to docs/boundary_map.yml to reflect legitimate areas of runtime ownership.
  - Add generated/build folders to tools/arch_checks/ignore_patterns.txt so they are excluded by default.
  - Prefer running the checks in Mode=changed (default in CI) so only changed files in PRs are validated.

Suggested immediate tuning
1. Add these patterns to tools/arch_checks/ignore_patterns.txt to suppress generated artifacts:

   build/**
   blazeclaw/BlazeClawMfc/models/**

2. Add broader allowed_paths to docs/boundary_map.yml for known runtime areas (work with owners to approve):

   - "blazeclaw/**"
   - "tools/**"

Artifacts
- Combined report: tools/arch_checks/arch_checks_report.json
- Per-check reports: tools/arch_checks/file_size_report.json, tools/arch_checks/boundary_report.json
