---
name: tool-dispatch
description: command with tool dispatch metadata
command-dispatch: tool
command-tool: tool.dispatch
command-arg-mode: raw
command-arg-schema: schema://tool.dispatch.args.v1
command-result-schema: schema://tool.dispatch.result.v1
command-idempotency-hint: safe
command-retry-policy-hint: transient-network
command-requires-approval: true
---

# Tool Dispatch
