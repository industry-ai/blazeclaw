/**
 * Self-Evolving Hook Handler (BlazeClaw Adapter)
 *
 * Phase 5 goal:
 * - Preserve self-improving-agent reminder behavior
 * - Remove direct dependency on OpenClaw hook runtime types
 *
 * Phase 6 will wire BlazeClaw runtime lifecycle events into this
 * normalized event contract.
 */

type BootstrapFile = {
  path: string;
  content: string;
  virtual?: boolean;
};

type BlazeClawHookEvent = {
  type?: string;
  action?: string;
  sessionKey?: string;
  context?: {
    bootstrapFiles?: BootstrapFile[];
  };
};

const REMINDER_CONTENT = `## Self-Evolving Reminder

After completing tasks, evaluate if any learnings should be captured:

**Log when:**
- User corrects you → \`.learnings/LEARNINGS.md\`
- Command/operation fails → \`.learnings/ERRORS.md\`
- User wants missing capability → \`.learnings/FEATURE_REQUESTS.md\`
- You discover your knowledge was wrong → \`.learnings/LEARNINGS.md\`
- You find a better approach → \`.learnings/LEARNINGS.md\`
- Outage simulation drill finishes → run \`scripts/outage-outcome-promoter.*\` with tenant, rollout phase, and policy profile

**Promote when pattern is proven:**
- Behavioral patterns → \`SOUL.md\`
- Workflow improvements → \`AGENTS.md\`
- Tool gotchas → \`TOOLS.md\`
- Policy tuning outcomes → \`.learnings/POLICY_TUNING_RECOMMENDATIONS.md\`

Keep entries simple: date, title, what happened, and what to do differently.`;

const handler = async (event: BlazeClawHookEvent): Promise<void> => {
  if (!event || typeof event !== 'object') {
    return;
  }

  if (event.type !== 'agent' || event.action !== 'bootstrap') {
    return;
  }

  if (!event.context || typeof event.context !== 'object') {
    return;
  }

  const sessionKey = event.sessionKey || '';
  if (sessionKey.includes(':subagent:')) {
    return;
  }

  if (Array.isArray(event.context.bootstrapFiles)) {
    event.context.bootstrapFiles.push({
      path: 'SELF_EVOLVING_REMINDER.md',
      content: REMINDER_CONTENT,
      virtual: true,
    });
  }
};

export default handler;
