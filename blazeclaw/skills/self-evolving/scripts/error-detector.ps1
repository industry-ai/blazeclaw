#!/usr/bin/env pwsh
# Self-Evolving Error Detector Hook (PowerShell)
# Detects likely command failures from tool output.
# Reads CLAUDE_TOOL_OUTPUT if available.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$output = if ($env:CLAUDE_TOOL_OUTPUT) { $env:CLAUDE_TOOL_OUTPUT } else { '' }

$errorPatterns = @(
    'error:',
    'Error:',
    'ERROR:',
    'failed',
    'FAILED',
    'command not found',
    'No such file',
    'Permission denied',
    'fatal:',
    'Exception',
    'Traceback',
    'npm ERR!',
    'ModuleNotFoundError',
    'SyntaxError',
    'TypeError',
    'exit code',
    'non-zero'
)

$containsError = $false
foreach ($pattern in $errorPatterns) {
    if ($output.Contains($pattern)) {
        $containsError = $true
        break
    }
}

if ($containsError) {
@'
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
'@
}
