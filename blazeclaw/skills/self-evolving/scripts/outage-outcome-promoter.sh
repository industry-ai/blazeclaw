#!/bin/bash
# Outage Outcome Promoter (BlazeClaw)
# Captures outage simulation outcomes and emits policy tuning recommendations.
# Adds tenant-scoped trend analysis and phase-aware recommendation scoring.

set -e

LEARNINGS_FILE="./blazeclaw/skills/self-evolving/.learnings/LEARNINGS.md"
POLICY_TUNING_FILE="./blazeclaw/skills/self-evolving/.learnings/POLICY_TUNING_RECOMMENDATIONS.md"
TREND_HISTORY_FILE="./blazeclaw/skills/self-evolving/.learnings/OUTAGE_TREND_HISTORY.csv"
PROFILE_WEIGHTS_FILE="./blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.csv"
PROFILE_MANIFEST_FILE="./blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest"

SIMULATION_ID=""
TENANT_ID=""
ROLLOUT_PHASE=""
POLICY_PROFILE="default"
STRICT_SCHEMA_VERSION=""
REQUIRE_SIGNED_MANIFEST=false
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
  --policy-profile      Policy profile for configurable score weights
  --dependency          Target dependency (registry|authority)
  --result              Simulation result (pass|fail)
  --evidence-path       Path to drill evidence artifact

Optional:
  --drill-window        Drill execution window (example: 2026-03-01T10:00Z..2026-03-01T10:30Z)
  --failure-mode        Failure mode exercised during drill
  --failover-triggered  Automated failover status (yes|no)
  --failback-completed  Automated failback status (yes|no)
  --weights-file        CSV file of per-profile scoring weights
  --manifest-file       Signed manifest for weight-file integrity verification
  --require-signed-manifest Enforce signed manifest verification gates
  --strict-schema-version Required schema version when strict validation is enabled
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
        --policy-profile)
            POLICY_PROFILE="${2:-}"
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
        --weights-file)
            PROFILE_WEIGHTS_FILE="${2:-}"
            shift 2
            ;;
        --manifest-file)
            PROFILE_MANIFEST_FILE="${2:-}"
            shift 2
            ;;
        --require-signed-manifest)
            REQUIRE_SIGNED_MANIFEST=true
            shift
            ;;
        --strict-schema-version)
            STRICT_SCHEMA_VERSION="${2:-}"
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

if [ -z "$POLICY_PROFILE" ]; then
    echo "--policy-profile cannot be empty" >&2
    exit 1
fi

if [ ! -f "$TREND_HISTORY_FILE" ]; then
    printf "%s\n" "timestamp,tenant_id,rollout_phase,dependency,result,simulation_id" > "$TREND_HISTORY_FILE"
fi

TIMESTAMP="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
DATE_STAMP="$(date -u +"%Y%m%d")"
SANITIZED_SIM_ID="$(echo "$SIMULATION_ID" | tr -cd 'A-Za-z0-9')"
ENTRY_ID="OUT-$DATE_STAMP-$SANITIZED_SIM_ID"

TENANT_HISTORY=$(awk -F',' -v tenant="$TENANT_ID" -v dep="$DEPENDENCY" 'NR > 1 && $2 == tenant && $4 == dep { print $0 }' "$TREND_HISTORY_FILE" | tail -n "$TREND_WINDOW_SIZE")
TREND_TOTAL=$(printf "%s\n" "$TENANT_HISTORY" | sed '/^$/d' | wc -l | tr -d ' ')
TREND_FAIL_COUNT=$(printf "%s\n" "$TENANT_HISTORY" | awk -F',' 'NF > 0 && $5 == "fail" { c++ } END { print c + 0 }')
TREND_PASS_COUNT=$(printf "%s\n" "$TENANT_HISTORY" | awk -F',' 'NF > 0 && $5 == "pass" { c++ } END { print c + 0 }')

TREND_FAIL_RATE=0
if [ "$TREND_TOTAL" -gt 0 ]; then
    TREND_FAIL_RATE=$(( TREND_FAIL_COUNT * 100 / TREND_TOTAL ))
fi

FAIL_BASE_SCORE=45
PASS_BASE_SCORE=10
PHASE_R1_WEIGHT=5
PHASE_R2_WEIGHT=10
PHASE_R3_WEIGHT=15
PHASE_R4_WEIGHT=20
DEPENDENCY_REGISTRY_WEIGHT=8
DEPENDENCY_AUTHORITY_WEIGHT=12
TREND_FAIL_DIVISOR=5
TREND_PASS_DIVISOR=10

if [ ! -f "$PROFILE_WEIGHTS_FILE" ]; then
    echo "Missing required profile weights file: $PROFILE_WEIGHTS_FILE" >&2
    exit 1
fi

if [ ! -r "$PROFILE_WEIGHTS_FILE" ]; then
    echo "Profile weights file is not readable: $PROFILE_WEIGHTS_FILE" >&2
    exit 1
fi

if [ "$REQUIRE_SIGNED_MANIFEST" = true ] || [ -n "$PROFILE_MANIFEST_FILE" ]; then
    if [ ! -f "$PROFILE_MANIFEST_FILE" ]; then
        echo "Missing required signed manifest file: $PROFILE_MANIFEST_FILE" >&2
        exit 1
    fi

    if [ ! -r "$PROFILE_MANIFEST_FILE" ]; then
        echo "Signed manifest file is not readable: $PROFILE_MANIFEST_FILE" >&2
        exit 1
    fi

    MANIFEST_VERSION=$(awk -F'=' '$1=="manifest_version" { print $2; exit }' "$PROFILE_MANIFEST_FILE" | tr -d '\r[:space:]')
    MANIFEST_WEIGHTS_FILE=$(awk -F'=' '$1=="weights_file" { print $2; exit }' "$PROFILE_MANIFEST_FILE" | tr -d '\r')
    MANIFEST_WEIGHTS_SHA256=$(awk -F'=' '$1=="weights_sha256" { print $2; exit }' "$PROFILE_MANIFEST_FILE" | tr -d '\r[:space:]')
    MANIFEST_SIGNATURE=$(awk -F'=' '$1=="signature" { print $2; exit }' "$PROFILE_MANIFEST_FILE" | tr -d '\r[:space:]')
    MANIFEST_SIGNED_BY=$(awk -F'=' '$1=="signed_by" { print $2; exit }' "$PROFILE_MANIFEST_FILE" | tr -d '\r')
    MANIFEST_SIGNED_AT=$(awk -F'=' '$1=="signed_at" { print $2; exit }' "$PROFILE_MANIFEST_FILE" | tr -d '\r[:space:]')
    MANIFEST_KEY_ID=$(awk -F'=' '$1=="key_id" { print $2; exit }' "$PROFILE_MANIFEST_FILE" | tr -d '\r[:space:]')

    if [ -z "$MANIFEST_VERSION" ] || [ -z "$MANIFEST_WEIGHTS_FILE" ] || [ -z "$MANIFEST_WEIGHTS_SHA256" ] || [ -z "$MANIFEST_SIGNATURE" ] || [ -z "$MANIFEST_SIGNED_BY" ] || [ -z "$MANIFEST_SIGNED_AT" ] || [ -z "$MANIFEST_KEY_ID" ]; then
        echo "Malformed signed manifest: required fields missing in $PROFILE_MANIFEST_FILE" >&2
        exit 1
    fi

    if ! [[ "$MANIFEST_WEIGHTS_SHA256" =~ ^[A-Fa-f0-9]{64}$ ]]; then
        echo "Malformed signed manifest: weights_sha256 must be 64-char hex in $PROFILE_MANIFEST_FILE" >&2
        exit 1
    fi

    if [ "$MANIFEST_WEIGHTS_FILE" != "$PROFILE_WEIGHTS_FILE" ]; then
        echo "Signed manifest mismatch: weights_file '$MANIFEST_WEIGHTS_FILE' does not match selected file '$PROFILE_WEIGHTS_FILE'" >&2
        exit 1
    fi

    if command -v sha256sum >/dev/null 2>&1; then
        ACTUAL_WEIGHTS_SHA256=$(sha256sum "$PROFILE_WEIGHTS_FILE" | awk '{print $1}')
    elif command -v shasum >/dev/null 2>&1; then
        ACTUAL_WEIGHTS_SHA256=$(shasum -a 256 "$PROFILE_WEIGHTS_FILE" | awk '{print $1}')
    else
        echo "Unable to verify signed manifest: sha256 tool not available" >&2
        exit 1
    fi

    if [ "${ACTUAL_WEIGHTS_SHA256,,}" != "${MANIFEST_WEIGHTS_SHA256,,}" ]; then
        echo "Signed manifest integrity failure: weights_sha256 mismatch for $PROFILE_WEIGHTS_FILE" >&2
        exit 1
    fi
fi

PROFILE_ROW=$(awk -F',' -v profile="$POLICY_PROFILE" 'NR > 1 && $1 == profile { print $0; exit }' "$PROFILE_WEIGHTS_FILE")
if [ -z "$PROFILE_ROW" ]; then
    echo "Missing required policy profile '$POLICY_PROFILE' in: $PROFILE_WEIGHTS_FILE" >&2
    exit 1
fi

HEADER_LINE=$(head -n 1 "$PROFILE_WEIGHTS_FILE" | tr -d '\r')
HAS_SCHEMA_COLUMN=false
if printf "%s" "$HEADER_LINE" | grep -Eq '(^|,)schema_version(,|$)'; then
    HAS_SCHEMA_COLUMN=true
fi

_schema=""
if [ "$HAS_SCHEMA_COLUMN" = true ]; then
    IFS=',' read -r _p _schema _fail _pass _r1 _r2 _r3 _r4 _reg _auth _tfd _tpd <<< "$PROFILE_ROW"
else
    IFS=',' read -r _p _fail _pass _r1 _r2 _r3 _r4 _reg _auth _tfd _tpd <<< "$PROFILE_ROW"
fi

_schema=$(echo "$_schema" | tr -d '\r[:space:]')
_fail=$(echo "$_fail" | tr -d '\r[:space:]')
_pass=$(echo "$_pass" | tr -d '\r[:space:]')
_r1=$(echo "$_r1" | tr -d '\r[:space:]')
_r2=$(echo "$_r2" | tr -d '\r[:space:]')
_r3=$(echo "$_r3" | tr -d '\r[:space:]')
_r4=$(echo "$_r4" | tr -d '\r[:space:]')
_reg=$(echo "$_reg" | tr -d '\r[:space:]')
_auth=$(echo "$_auth" | tr -d '\r[:space:]')
_tfd=$(echo "$_tfd" | tr -d '\r[:space:]')
_tpd=$(echo "$_tpd" | tr -d '\r[:space:]')

CANDIDATES=("$_fail" "$_pass" "$_r1" "$_r2" "$_r3" "$_r4" "$_reg" "$_auth" "$_tfd" "$_tpd")
VALID=true
for v in "${CANDIDATES[@]}"; do
    if ! [[ "$v" =~ ^[0-9]+$ ]]; then
        VALID=false
        break
    fi
done

if [ "$VALID" != true ]; then
    echo "Malformed profile '$POLICY_PROFILE': expected numeric weights in $PROFILE_WEIGHTS_FILE" >&2
    exit 1
fi

if [ -n "$STRICT_SCHEMA_VERSION" ]; then
    if [ "$HAS_SCHEMA_COLUMN" != true ]; then
        echo "Malformed profile weights file: schema_version column is required for --strict-schema-version in $PROFILE_WEIGHTS_FILE" >&2
        exit 1
    fi

    if [ -z "$_schema" ]; then
        echo "Malformed profile '$POLICY_PROFILE': schema_version value is required for --strict-schema-version in $PROFILE_WEIGHTS_FILE" >&2
        exit 1
    fi

    if [ "$_schema" != "$STRICT_SCHEMA_VERSION" ]; then
        echo "Schema version mismatch for profile '$POLICY_PROFILE': expected '$STRICT_SCHEMA_VERSION' but found '$_schema' in $PROFILE_WEIGHTS_FILE" >&2
        exit 1
    fi
fi

if [ "$_tfd" -le 0 ] || [ "$_tpd" -le 0 ]; then
    echo "Malformed profile '$POLICY_PROFILE': trend divisors must be > 0 in $PROFILE_WEIGHTS_FILE" >&2
    exit 1
fi

FAIL_BASE_SCORE=$_fail
PASS_BASE_SCORE=$_pass
PHASE_R1_WEIGHT=$_r1
PHASE_R2_WEIGHT=$_r2
PHASE_R3_WEIGHT=$_r3
PHASE_R4_WEIGHT=$_r4
DEPENDENCY_REGISTRY_WEIGHT=$_reg
DEPENDENCY_AUTHORITY_WEIGHT=$_auth
TREND_FAIL_DIVISOR=$_tfd
TREND_PASS_DIVISOR=$_tpd

PHASE_WEIGHT=0
case "$ROLLOUT_PHASE" in
    r1) PHASE_WEIGHT=$PHASE_R1_WEIGHT ;;
    r2) PHASE_WEIGHT=$PHASE_R2_WEIGHT ;;
    r3) PHASE_WEIGHT=$PHASE_R3_WEIGHT ;;
    r4) PHASE_WEIGHT=$PHASE_R4_WEIGHT ;;
esac

DEPENDENCY_WEIGHT=$DEPENDENCY_REGISTRY_WEIGHT
if [ "$DEPENDENCY" = "authority" ]; then
    DEPENDENCY_WEIGHT=$DEPENDENCY_AUTHORITY_WEIGHT
fi

RECOMMENDATION_SCORE=0
if [ "$RESULT" = "fail" ]; then
    RECOMMENDATION_SCORE=$(( FAIL_BASE_SCORE + PHASE_WEIGHT + DEPENDENCY_WEIGHT + (TREND_FAIL_RATE / TREND_FAIL_DIVISOR) ))
else
    RECOMMENDATION_SCORE=$(( PASS_BASE_SCORE + (PHASE_WEIGHT / 2) + (DEPENDENCY_WEIGHT / 2) + (TREND_FAIL_RATE / TREND_PASS_DIVISOR) ))
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
- Policy Profile: $POLICY_PROFILE
- Policy Profile Schema Version: ${_schema:-not-declared}
- Dependency: $DEPENDENCY
- Result: $RESULT
- Failure Mode: ${FAILURE_MODE:-not-provided}
- Drill Window: ${DRILL_WINDOW:-not-provided}
- Evidence Path: $EVIDENCE_PATH
- Automated Failover Triggered: ${FAILOVER_TRIGGERED:-not-provided}
- Automated Failback Completed: ${FAILBACK_COMPLETED:-not-provided}
- Trend Window Size: $TREND_WINDOW_SIZE
- Trend Segment: tenant=$TENANT_ID, dependency=$DEPENDENCY
- Trend Sample Count: $TREND_TOTAL
- Trend Fail Count: $TREND_FAIL_COUNT
- Trend Pass Count: $TREND_PASS_COUNT
- Trend Fail Rate: ${TREND_FAIL_RATE}%

### Suggested Action
$RECOMMENDATION

### Metadata
- Source: outage_simulation
- Related Files: references/enterprise-policy-attestation-publication-template.md, references/hook-rollout-policy-template.md
- Tags: outage-simulation, failover, policy-tuning, governance, tenant-scoped, phase-aware, profile-weighted

---
EOF
)

POLICY_ENTRY=$(cat << EOF
## [REC-$ENTRY_ID] policy_tuning

**Logged**: $TIMESTAMP
**Source Simulation**: $SIMULATION_ID
**Tenant ID**: $TENANT_ID
**Rollout Phase**: $ROLLOUT_PHASE
**Policy Profile**: $POLICY_PROFILE
**Dependency**: $DEPENDENCY
**Outcome**: $RESULT
**Status**: suggested

### Recommendation
$RECOMMENDATION

### Scoring
- Recommendation Score: $RECOMMENDATION_SCORE
- Severity: $SEVERITY
- Trend Window Size: $TREND_WINDOW_SIZE
- Trend Segment: tenant=$TENANT_ID, dependency=$DEPENDENCY
- Trend Sample Count: $TREND_TOTAL
- Trend Fail Count: $TREND_FAIL_COUNT
- Trend Pass Count: $TREND_PASS_COUNT
- Trend Fail Rate: ${TREND_FAIL_RATE}%
- Weight Source: $PROFILE_WEIGHTS_FILE
- Manifest Source: $PROFILE_MANIFEST_FILE
- Manifest Version: ${MANIFEST_VERSION:-not-verified}
- Signed By: ${MANIFEST_SIGNED_BY:-not-verified}
- Signed At: ${MANIFEST_SIGNED_AT:-not-verified}
- Key Id: ${MANIFEST_KEY_ID:-not-verified}
- Manifest Signature: ${MANIFEST_SIGNATURE:-not-verified}
- Weight Schema Version: ${_schema:-not-declared}
- Strict Schema Gate: ${STRICT_SCHEMA_VERSION:-disabled}
- Strict Manifest Gate: ${REQUIRE_SIGNED_MANIFEST}
- Weight Inputs:
  - fail-base=$FAIL_BASE_SCORE
  - pass-base=$PASS_BASE_SCORE
  - phase-r1=$PHASE_R1_WEIGHT
  - phase-r2=$PHASE_R2_WEIGHT
  - phase-r3=$PHASE_R3_WEIGHT
  - phase-r4=$PHASE_R4_WEIGHT
  - dependency-registry=$DEPENDENCY_REGISTRY_WEIGHT
  - dependency-authority=$DEPENDENCY_AUTHORITY_WEIGHT
  - trend-fail-divisor=$TREND_FAIL_DIVISOR
  - trend-pass-divisor=$TREND_PASS_DIVISOR

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
