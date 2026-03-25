#!/bin/bash
# Self-Evolving Activator Hook
# Emits a lightweight reminder after prompt submission.
# Keep output minimal to reduce prompt overhead.

set -e

cat << 'EOF'
<self-evolving-reminder>
After completing this task, evaluate whether reusable knowledge emerged:
- Non-obvious solution discovered through investigation?
- Workaround for unexpected behavior?
- Project-specific pattern learned?
- Error required debugging to resolve?

If yes: Log to blazeclaw/skills/self-evolving/.learnings/.
If high-value (recurring, broadly applicable): Consider extracting a reusable skill.
</self-evolving-reminder>
EOF
