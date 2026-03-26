#!/bin/bash
# Outage Outcome Promoter (BlazeClaw)
# Captures outage simulation outcomes and emits policy tuning recommendations.
# Usage:
#   ./outage-outcome-promoter.sh --simulation-id SIM-REG-001 --dependency registry --result pass --evidence-path reports/drills/registry.json

set -e

LEARNINGS_FILE="./blazeclaw/skills/self-evolving/.learnings/LEARNINGS.md"
POLICY_TUNING_FILE="./blazeclaw/skills/self-evolving/.learnings/POLICY_TUNING_RECOMMENDATIONS.md"

SIMULATION_ID=""
DEPENDENCY=""
RESULT=""
EVIDENCE_PATH=""
DRILL_WINDOW=""
FAILURE_MODE=""
FAILOVER_TRIGGERED=""
FAILBACK_COMPLETED=""
NOTES=""
DRY_RUN=false

usage() {
    cat << EOF
Usage: $(basename "$0") --simulation-id <id> --dependency <registry|authority> --result <pass|fail> --evidence-path <path> [options]

Required:
  --simulation-id       Outage simulation identifier (example: SIM-REG-001)
  --dependency          Target dependency (registry|authority)
  --result              Simulation result (pass|fail)
  --evidence-path       Path to drill evidence artifact

Optional:
  --drill-window        Drill execution window (example: 2026-03-01T10:00Z..2026-03-01T10:30Z)
  --failure-mode        Failure mode exercised during drill
  --failover-triggered  Automated failover status (yes|no)
  --failback-completed  Automated failback status (yes|no)
  --notes               Additional context
  --dry-run             Print entries without writing files
  -h, --help            Show this help message
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --simulation-id)
            SIMULATION_ID="${2:-}"
            shift 2
            ;;
        --dependency)
            DEPENDENCY="${2:-}"
            shift 2
            ;;
        --result)
            RESULT="${2:-}"
            shift 2
            ;;
        --evidence-path)
            EVIDENCE_PATH="${2:-}"
            shift 2
            ;;
        --drill-window)
            DRILL_WINDOW="${2:-}"
            shift 2
            ;;
        --failure-mode)
            FAILURE_MODE="${2:-}"
            shift 2
            ;;
        --failover-triggered)
            FAILOVER_TRIGGERED="${2:-}"
            shift 2
            ;;
        --failback-completed)
            FAILBACK_COMPLETED="${2:-}"
            shift 2
            ;;
        --notes)
            NOTES="${2:-}"
            shift 2
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [ -z "$SIMULATION_ID" ] || [ -z "$DEPENDENCY" ] || [ -z "$RESULT" ] || [ -z "$EVIDENCE_PATH" ]; then
    echo "Missing required arguments." >&2
    usage
    exit 1
fi

if [[ "$DEPENDENCY" != "registry" && "$DEPENDENCY" != "authority" ]]; then
    echo "--dependency must be one of: registry, authority" >&2
    exit 1
fi

if [[ "$RESULT" != "pass" && "$RESULT" != "fail" ]]; then
    echo "--result must be one of: pass, fail" >&2
    exit 1
fi

TIMESTAMP="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
DATE_STAMP="$(date -u +"%Y%m%d")"
SANITIZED_SIM_ID="$(echo "$SIMULATION_ID" | tr -cd 'A-Za-z0-9')"
ENTRY_ID="OUT-$DATE_STAMP-$SANITIZED_SIM_ID"

CATEGORY="best_practice"
STATUS="promoted"
if [ "$RESULT" = "fail" ]; then
    CATEGORY="knowledge_gap"
    STATUS="pending"
fi

RECOMMENDATION=""
CONTROL_KEYS=""
if [ "$DEPENDENCY" = "registry" ]; then
    CONTROL_KEYS="hooks.engine.policyRegistrySyncMode, hooks.engine.registryOutageSimulationEnabled, hooks.engine.registryFailoverRunbookId"
    if [ "$RESULT" = "pass" ]; then
        RECOMMENDATION="Keep registry failover controls enabled and tighten sync health alert thresholds for promotion gates."
    else
        RECOMMENDATION="Enable registry outage simulation and enforce fallback bundle pin validation before promotion."
    fi
else
    CONTROL_KEYS="hooks.engine.attestationRevocationMode, hooks.engine.authorityOutageSimulationEnabled, hooks.engine.authorityFailoverRunbookId"
    if [ "$RESULT" = "pass" ]; then
        RECOMMENDATION="Retain authority failover endpoint coverage and reduce tolerated verification latency in rollout policy."
    else
        RECOMMENDATION="Enforce strict authority trust-chain validation and block publication until failover verification recovers."
    fi
fi

LEARNING_ENTRY=$(cat << EOF
## [LRN-$ENTRY_ID] $CATEGORY

**Logged**: $TIMESTAMP
**Priority**: high
**Status**: $STATUS
**Promoted**: AGENTS.md
**Area**: infra

### Summary
Outage simulation $SIMULATION_ID reported $RESULT for $DEPENDENCY dependency

### Details
- Simulation ID: $SIMULATION_ID
- Dependency: $DEPENDENCY
- Result: $RESULT
- Failure Mode: ${FAILURE_MODE:-not-provided}
- Drill Window: ${DRILL_WINDOW:-not-provided}
- Evidence Path: $EVIDENCE_PATH
- Automated Failover Triggered: ${FAILOVER_TRIGGERED:-not-provided}
- Automated Failback Completed: ${FAILBACK_COMPLETED:-not-provided}

### Suggested Action
$RECOMMENDATION

### Metadata
- Source: outage_simulation
- Related Files: references/enterprise-policy-attestation-publication-template.md, references/hook-rollout-policy-template.md
- Tags: outage-simulation, failover, policy-tuning, governance

---
EOF
)

POLICY_ENTRY=$(cat << EOF
## [REC-$ENTRY_ID] policy_tuning

**Logged**: $TIMESTAMP
**Source Simulation**: $SIMULATION_ID
**Dependency**: $DEPENDENCY
**Outcome**: $RESULT
**Status**: suggested

### Recommendation
$RECOMMENDATION

### Target Controls
- $CONTROL_KEYS

### Evidence
- Drill Report: $EVIDENCE_PATH
- Failure Mode: ${FAILURE_MODE:-not-provided}
- Drill Window: ${DRILL_WINDOW:-not-provided}
- Automated Failover Triggered: ${FAILOVER_TRIGGERED:-not-provided}
- Automated Failback Completed: ${FAILBACK_COMPLETED:-not-provided}

### Notes
${NOTES:-none}

---
EOF
)

if [ "$DRY_RUN" = true ]; then
    echo "[DRY-RUN] Would append to $LEARNINGS_FILE:"
    echo "$LEARNING_ENTRY"
    echo "[DRY-RUN] Would append to $POLICY_TUNING_FILE:"
    echo "$POLICY_ENTRY"
    exit 0
fi

if [ ! -f "$LEARNINGS_FILE" ]; then
    echo "Missing learnings file: $LEARNINGS_FILE" >&2
    exit 1
fi

if [ ! -f "$POLICY_TUNING_FILE" ]; then
    echo "Missing policy tuning file: $POLICY_TUNING_FILE" >&2
    exit 1
fi

printf "%s\n" "$LEARNING_ENTRY" >> "$LEARNINGS_FILE"
printf "%s\n" "$POLICY_ENTRY" >> "$POLICY_TUNING_FILE"

echo "Appended outage simulation learning entry to $LEARNINGS_FILE"
echo "Appended policy tuning recommendation to $POLICY_TUNING_FILE"
