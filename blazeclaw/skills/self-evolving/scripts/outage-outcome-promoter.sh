#!/bin/bash
# Outage Outcome Promoter (BlazeClaw)
# Captures outage simulation outcomes and emits policy tuning recommendations.
# Adds tenant-scoped trend analysis and phase-aware recommendation scoring.

set -e

LEARNINGS_FILE="./blazeclaw/skills/self-evolving/.learnings/LEARNINGS.md"
POLICY_TUNING_FILE="./blazeclaw/skills/self-evolving/.learnings/POLICY_TUNING_RECOMMENDATIONS.md"
TREND_HISTORY_FILE="./blazeclaw/skills/self-evolving/.learnings/OUTAGE_TREND_HISTORY.csv"

SIMULATION_ID=""
TENANT_ID=""
ROLLOUT_PHASE=""
DEPENDENCY=""
RESULT=""
EVIDENCE_PATH=""
DRILL_WINDOW=""
FAILURE_MODE=""
FAILOVER_TRIGGERED=""
FAILBACK_COMPLETED=""
NOTES=""
TREND_WINDOW_SIZE=20
DRY_RUN=false

usage() {
    cat << EOF
Usage: $(basename "$0") --simulation-id <id> --tenant-id <tenant> --rollout-phase <r1|r2|r3|r4> --dependency <registry|authority> --result <pass|fail> --evidence-path <path> [options]

Required:
  --simulation-id       Outage simulation identifier (example: SIM-REG-001)
  --tenant-id           Tenant identifier for trend analysis
  --rollout-phase       Rollout phase (r1|r2|r3|r4)
  --dependency          Target dependency (registry|authority)
  --result              Simulation result (pass|fail)
  --evidence-path       Path to drill evidence artifact

Optional:
  --drill-window        Drill execution window (example: 2026-03-01T10:00Z..2026-03-01T10:30Z)
  --failure-mode        Failure mode exercised during drill
  --failover-triggered  Automated failover status (yes|no)
  --failback-completed  Automated failback status (yes|no)
  --trend-window-size   Number of recent tenant outcomes to analyze (default: 20)
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
        --tenant-id)
            TENANT_ID="${2:-}"
            shift 2
            ;;
        --rollout-phase)
            ROLLOUT_PHASE="${2:-}"
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
        --trend-window-size)
            TREND_WINDOW_SIZE="${2:-}"
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

if [ -z "$SIMULATION_ID" ] || [ -z "$TENANT_ID" ] || [ -z "$ROLLOUT_PHASE" ] || [ -z "$DEPENDENCY" ] || [ -z "$RESULT" ] || [ -z "$EVIDENCE_PATH" ]; then
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

if [[ "$ROLLOUT_PHASE" != "r1" && "$ROLLOUT_PHASE" != "r2" && "$ROLLOUT_PHASE" != "r3" && "$ROLLOUT_PHASE" != "r4" ]]; then
    echo "--rollout-phase must be one of: r1, r2, r3, r4" >&2
    exit 1
fi

if ! [[ "$TREND_WINDOW_SIZE" =~ ^[0-9]+$ ]] || [ "$TREND_WINDOW_SIZE" -le 0 ]; then
    echo "--trend-window-size must be a positive integer" >&2
    exit 1
fi

if [ ! -f "$TREND_HISTORY_FILE" ]; then
    printf "%s\n" "timestamp,tenant_id,rollout_phase,dependency,result,simulation_id" > "$TREND_HISTORY_FILE"
fi

TIMESTAMP="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
DATE_STAMP="$(date -u +"%Y%m%d")"
SANITIZED_SIM_ID="$(echo "$SIMULATION_ID" | tr -cd 'A-Za-z0-9')"
ENTRY_ID="OUT-$DATE_STAMP-$SANITIZED_SIM_ID"

TENANT_HISTORY=$(awk -F',' -v tenant="$TENANT_ID" 'NR > 1 && $2 == tenant { print $0 }' "$TREND_HISTORY_FILE" | tail -n "$TREND_WINDOW_SIZE")
TREND_TOTAL=$(printf "%s\n" "$TENANT_HISTORY" | sed '/^$/d' | wc -l | tr -d ' ')
TREND_FAIL_COUNT=$(printf "%s\n" "$TENANT_HISTORY" | awk -F',' 'NF > 0 && $5 == "fail" { c++ } END { print c + 0 }')
TREND_PASS_COUNT=$(printf "%s\n" "$TENANT_HISTORY" | awk -F',' 'NF > 0 && $5 == "pass" { c++ } END { print c + 0 }')

TREND_FAIL_RATE=0
if [ "$TREND_TOTAL" -gt 0 ]; then
    TREND_FAIL_RATE=$(( TREND_FAIL_COUNT * 100 / TREND_TOTAL ))
fi

PHASE_WEIGHT=0
case "$ROLLOUT_PHASE" in
    r1) PHASE_WEIGHT=5 ;;
    r2) PHASE_WEIGHT=10 ;;
    r3) PHASE_WEIGHT=15 ;;
    r4) PHASE_WEIGHT=20 ;;
esac

DEPENDENCY_WEIGHT=8
if [ "$DEPENDENCY" = "authority" ]; then
    DEPENDENCY_WEIGHT=12
fi

RECOMMENDATION_SCORE=0
if [ "$RESULT" = "fail" ]; then
    RECOMMENDATION_SCORE=$(( 45 + PHASE_WEIGHT + DEPENDENCY_WEIGHT + (TREND_FAIL_RATE / 5) ))
else
    RECOMMENDATION_SCORE=$(( 10 + (PHASE_WEIGHT / 2) + (DEPENDENCY_WEIGHT / 2) + (TREND_FAIL_RATE / 10) ))
fi

if [ "$RECOMMENDATION_SCORE" -gt 100 ]; then
    RECOMMENDATION_SCORE=100
fi

SEVERITY="low"
if [ "$RECOMMENDATION_SCORE" -ge 80 ]; then
    SEVERITY="critical"
elif [ "$RECOMMENDATION_SCORE" -ge 60 ]; then
    SEVERITY="high"
elif [ "$RECOMMENDATION_SCORE" -ge 35 ]; then
    SEVERITY="medium"
fi

CATEGORY="best_practice"
STATUS="promoted"
if [ "$RESULT" = "fail" ]; then
    CATEGORY="knowledge_gap"
    STATUS="pending"
fi

BASE_RECOMMENDATION=""
CONTROL_KEYS=""
if [ "$DEPENDENCY" = "registry" ]; then
    CONTROL_KEYS="hooks.engine.policyRegistrySyncMode, hooks.engine.registryOutageSimulationEnabled, hooks.engine.registryFailoverRunbookId"
    if [ "$RESULT" = "pass" ]; then
        BASE_RECOMMENDATION="Keep registry failover controls enabled and tighten sync health alert thresholds for promotion gates."
    else
        BASE_RECOMMENDATION="Enable registry outage simulation and enforce fallback bundle pin validation before promotion."
    fi
else
    CONTROL_KEYS="hooks.engine.attestationRevocationMode, hooks.engine.authorityOutageSimulationEnabled, hooks.engine.authorityFailoverRunbookId"
    if [ "$RESULT" = "pass" ]; then
        BASE_RECOMMENDATION="Retain authority failover endpoint coverage and reduce tolerated verification latency in rollout policy."
    else
        BASE_RECOMMENDATION="Enforce strict authority trust-chain validation and block publication until failover verification recovers."
    fi
fi

PHASE_GUIDANCE="Phase $ROLLOUT_PHASE score band is $SEVERITY (score: $RECOMMENDATION_SCORE)."
if [ "$RESULT" = "fail" ] && [[ "$ROLLOUT_PHASE" == "r3" || "$ROLLOUT_PHASE" == "r4" ]]; then
    PHASE_GUIDANCE="$PHASE_GUIDANCE Promotion should be blocked until remediation evidence is verified."
elif [ "$RESULT" = "pass" ] && [ "$SEVERITY" = "low" ]; then
    PHASE_GUIDANCE="$PHASE_GUIDANCE Eligible for next phase with routine monitoring."
fi

RECOMMENDATION="$BASE_RECOMMENDATION $PHASE_GUIDANCE"

LEARNING_ENTRY=$(cat << EOF
## [LRN-$ENTRY_ID] $CATEGORY

**Logged**: $TIMESTAMP
**Priority**: high
**Status**: $STATUS
**Promoted**: AGENTS.md
**Area**: infra

### Summary
Outage simulation $SIMULATION_ID reported $RESULT for $DEPENDENCY dependency in tenant $TENANT_ID ($ROLLOUT_PHASE)

### Details
- Simulation ID: $SIMULATION_ID
- Tenant ID: $TENANT_ID
- Rollout Phase: $ROLLOUT_PHASE
- Dependency: $DEPENDENCY
- Result: $RESULT
- Failure Mode: ${FAILURE_MODE:-not-provided}
- Drill Window: ${DRILL_WINDOW:-not-provided}
- Evidence Path: $EVIDENCE_PATH
- Automated Failover Triggered: ${FAILOVER_TRIGGERED:-not-provided}
- Automated Failback Completed: ${FAILBACK_COMPLETED:-not-provided}
- Trend Window Size: $TREND_WINDOW_SIZE
- Trend Sample Count: $TREND_TOTAL
- Trend Fail Count: $TREND_FAIL_COUNT
- Trend Pass Count: $TREND_PASS_COUNT
- Trend Fail Rate: ${TREND_FAIL_RATE}%

### Suggested Action
$RECOMMENDATION

### Metadata
- Source: outage_simulation
- Related Files: references/enterprise-policy-attestation-publication-template.md, references/hook-rollout-policy-template.md
- Tags: outage-simulation, failover, policy-tuning, governance, tenant-scoped, phase-aware

---
EOF
)

POLICY_ENTRY=$(cat << EOF
## [REC-$ENTRY_ID] policy_tuning

**Logged**: $TIMESTAMP
**Source Simulation**: $SIMULATION_ID
**Tenant ID**: $TENANT_ID
**Rollout Phase**: $ROLLOUT_PHASE
**Dependency**: $DEPENDENCY
**Outcome**: $RESULT
**Status**: suggested

### Recommendation
$RECOMMENDATION

### Scoring
- Recommendation Score: $RECOMMENDATION_SCORE
- Severity: $SEVERITY
- Trend Window Size: $TREND_WINDOW_SIZE
- Trend Sample Count: $TREND_TOTAL
- Trend Fail Count: $TREND_FAIL_COUNT
- Trend Pass Count: $TREND_PASS_COUNT
- Trend Fail Rate: ${TREND_FAIL_RATE}%

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

TREND_RECORD="$TIMESTAMP,$TENANT_ID,$ROLLOUT_PHASE,$DEPENDENCY,$RESULT,$SIMULATION_ID"

if [ "$DRY_RUN" = true ]; then
    echo "[DRY-RUN] Would append to $LEARNINGS_FILE:"
    echo "$LEARNING_ENTRY"
    echo "[DRY-RUN] Would append to $POLICY_TUNING_FILE:"
    echo "$POLICY_ENTRY"
    echo "[DRY-RUN] Would append trend record to $TREND_HISTORY_FILE:"
    echo "$TREND_RECORD"
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
printf "%s\n" "$TREND_RECORD" >> "$TREND_HISTORY_FILE"

echo "Appended outage simulation learning entry to $LEARNINGS_FILE"
echo "Appended policy tuning recommendation to $POLICY_TUNING_FILE"
echo "Appended tenant trend record to $TREND_HISTORY_FILE"
