#!/bin/bash
# Self-Evolving Error Detector Hook
# Detects likely command failures from tool output.
# Reads CLAUDE_TOOL_OUTPUT if available.

set -e

OUTPUT="${CLAUDE_TOOL_OUTPUT:-}"

ERROR_PATTERNS=(
    "error:"
    "Error:"
    "ERROR:"
    "failed"
    "FAILED"
    "command not found"
    "No such file"
    "Permission denied"
    "fatal:"
    "Exception"
    "Traceback"
    "npm ERR!"
    "ModuleNotFoundError"
    "SyntaxError"
    "TypeError"
    "exit code"
    "non-zero"
)

contains_error=false
for pattern in "${ERROR_PATTERNS[@]}"; do
    if [[ "$OUTPUT" == *"$pattern"* ]]; then
        contains_error=true
        break
    fi
done

if [ "$contains_error" = true ]; then
    cat << 'EOF'
<self-evolving-error-detected>
A command error was detected. Consider logging to:
- blazeclaw/skills/self-evolving/.learnings/ERRORS.md

Log it when:
- The error was unexpected or non-obvious
- It required investigation to resolve
- It might recur in similar contexts
- The solution could benefit future sessions

Use entry format: [ERR-YYYYMMDD-XXX]
</self-evolving-error-detected>
EOF
fi
