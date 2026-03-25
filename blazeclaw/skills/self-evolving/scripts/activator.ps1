#!/usr/bin/env pwsh
# Self-Evolving Activator Hook (PowerShell)
# Emits a lightweight reminder after prompt submission.
# Keep output minimal to reduce prompt overhead.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

@'
<self-evolving-reminder>
After completing this task, evaluate whether reusable knowledge emerged:
- Non-obvious solution discovered through investigation?
- Workaround for unexpected behavior?
- Project-specific pattern learned?
- Error required debugging to resolve?

If yes: Log to blazeclaw/skills/self-evolving/.learnings/.
If high-value (recurring, broadly applicable): Consider extracting a reusable skill.
</self-evolving-reminder>
'@
