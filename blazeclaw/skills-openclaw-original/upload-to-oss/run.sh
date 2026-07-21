#!/usr/bin/env bash
# upload-to-oss/run.sh — thin wrapper that finds Python and calls main.py
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Detect Python
PYTHON=""
for py in python3 python py; do
    if command -v "$py" >/dev/null 2>&1; then
        PYTHON="$py"
        break
    fi
done
if [ -z "$PYTHON" ]; then
    echo "Error: Python not found" >&2
    exit 1
fi

exec "$PYTHON" "$SCRIPT_DIR/main.py" "$@"
