#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

PYTHON=""
for py in python3 python py; do
    if command -v "$py" >/dev/null 2>&1; then
        PYTHON="$py"
        break
    fi
done
exec "$PYTHON" "$SCRIPT_DIR/test_cos_skill.py"
