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
TRUST_POLICY_FILE="./blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.conf"
REVOCATION_LIST_FILE="./blazeclaw/skills/self-evolving/assets/policy-profile-key-revocations.csv"
TRUST_POLICY_ATTESTATION_FILE="./blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.attestation"
REVOCATION_SLO_FILE="./blazeclaw/skills/self-evolving/assets/policy-profile-revocation-slo.conf"
ATTESTATION_DASHBOARD_FILE="./blazeclaw/skills/self-evolving/.learnings/TENANT_TRUST_POLICY_ATTESTATION_DASHBOARD.md"
ATTESTATION_TREND_HISTORY_FILE="./blazeclaw/skills/self-evolving/.learnings/TENANT_TRUST_POLICY_ATTESTATION_HISTORY.csv"
CROSS_TENANT_HEATMAP_FILE="./blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_ATTESTATION_ANOMALY_HEATMAP.md"
AUTO_REMEDIATION_ROUTING_FILE="./blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_AUTO_REMEDIATION_ROUTING.md"
TENANT_CRITICALITY_FILE="./blazeclaw/skills/self-evolving/assets/tenant-criticality-tiers.csv"
ADAPTIVE_THRESHOLD_POLICY_FILE="./blazeclaw/skills/self-evolving/assets/attestation-anomaly-threshold-tiers.csv"
TIME_DECAY_POLICY_FILE="./blazeclaw/skills/self-evolving/assets/attestation-anomaly-time-decay-policy.conf"
RECURRENCE_TUNING_POLICY_FILE="./blazeclaw/skills/self-evolving/assets/attestation-anomaly-recurrence-tuning-policy.conf"
SEASONAL_DECOMPOSITION_POLICY_FILE="./blazeclaw/skills/self-evolving/assets/attestation-anomaly-seasonal-decomposition-policy.conf"
SEASONAL_OVERLAY_POLICY_FILE="./blazeclaw/skills/self-evolving/assets/attestation-anomaly-seasonal-overlay-policy.csv"
CAUSAL_CLUSTERING_POLICY_FILE="./blazeclaw/skills/self-evolving/assets/attestation-anomaly-causal-clustering-policy.conf"
CAUSAL_HISTORY_FILE="./blazeclaw/skills/self-evolving/.learnings/ANOMALY_CAUSAL_HISTORY.csv"
OVERLAY_CANDIDATE_FILE="./blazeclaw/skills/self-evolving/.learnings/SEASONAL_OVERLAY_CANDIDATES.md"
CAUSAL_GRAPHING_POLICY_FILE="./blazeclaw/skills/self-evolving/assets/attestation-anomaly-causal-graphing-policy.conf"
CAUSAL_GRAPH_FILE="./blazeclaw/skills/self-evolving/.learnings/CAUSAL_CONFIDENCE_GRAPH.md"

SIMULATION_ID=""
TENANT_ID=""
ROLLOUT_PHASE=""
POLICY_PROFILE="default"
STRICT_SCHEMA_VERSION=""
REQUIRE_SIGNED_MANIFEST=false
REQUIRE_TRUST_POLICY=false
REQUIRE_REVOCATION_CHECK=false
REQUIRE_TRUST_POLICY_ATTESTATION=false
REQUIRE_REVOCATION_SLO=false
ENABLE_ATTESTATION_DASHBOARD=true
ENABLE_CROSS_TENANT_HEATMAP=true
ATTESTATION_BASELINE_WINDOW=20
ATTESTATION_ANOMALY_THRESHOLD_PERCENT=25
REQUIRE_ATTESTATION_BASELINE_GATE=false
ENABLE_ADAPTIVE_THRESHOLD_CALIBRATION=true
REQUIRE_ADAPTIVE_THRESHOLD_POLICY=false
ENABLE_TIME_DECAY_WEIGHTING=true
TIME_DECAY_HALF_LIFE=5
REQUIRE_TIME_DECAY_POLICY=false
ENABLE_RECURRENCE_AUTO_TUNING=true
REQUIRE_RECURRENCE_TUNING_POLICY=false
ENABLE_SEASONAL_RECURRENCE_DECOMPOSITION=true
REPORTING_CYCLE_LENGTH=12
REQUIRE_SEASONAL_DECOMPOSITION_POLICY=false
ENABLE_SEASONAL_OVERLAY_TUNING=true
REQUIRE_SEASONAL_OVERLAY_POLICY=false
ENABLE_CAUSAL_CLUSTERING=true
REQUIRE_CAUSAL_CLUSTERING_POLICY=false
ENABLE_CONFIDENCE_CAUSAL_GRAPHING=true
REQUIRE_CAUSAL_GRAPHING_POLICY=false
ENABLE_GRAPH_EDGE_PERSISTENCE=true
GRAPH_COHORT_WINDOW=30
GRAPH_TEMPORAL_DECAY_RATE=0.15
MANIFEST_FILE_EXPLICIT=false
SIGNATURE_VERIFICATION_MODE="none"
KMS_PUBLIC_KEY_FILE=""
SIGSTORE_CERTIFICATE_FILE=""
SIGSTORE_CERTIFICATE_IDENTITY=""
SIGSTORE_OIDC_ISSUER=""
COSIGN_PATH="cosign"
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
  --trust-policy-file   Trust-policy distribution file for key rotation checks
  --require-trust-policy Enforce trust-policy allow/revoke checks
  --revocation-file     Revocation list file for key revocation checks
  --require-revocation-check Enforce revocation-list checks
  --trust-policy-attestation-file Trust-policy publication attestation file
  --require-trust-policy-attestation Enforce trust-policy publication attestation checks
  --revocation-slo-file Revocation propagation SLO policy file
  --require-revocation-slo Enforce revocation propagation SLO checks
  --attestation-dashboard-file Tenant attestation dashboard markdown output
  --attestation-history-file Tenant attestation trend history CSV output
  --attestation-baseline-window Baseline sample window size (default: 20)
  --attestation-anomaly-threshold-percent Anomaly threshold percent (default: 25)
  --require-attestation-baseline-gate Fail-fast on tenant anomaly threshold breach
  --disable-attestation-dashboard Skip dashboard/trend file writes
  --cross-tenant-heatmap-file Cross-tenant anomaly heatmap markdown output
  --auto-remediation-routing-file Cross-tenant remediation routing markdown output
  --disable-cross-tenant-heatmap Skip cross-tenant heatmap and routing writes
  --tenant-criticality-file Tenant criticality tier mapping CSV
  --adaptive-threshold-policy-file Tier-to-threshold mapping CSV
  --disable-adaptive-threshold-calibration Use static threshold only
  --require-adaptive-threshold-policy Fail-fast when adaptive tier policy cannot be resolved
  --time-decay-policy-file Time-decay policy file for weighted anomaly model
  --time-decay-half-life Baseline half-life in samples for recency decay (default: 5)
  --disable-time-decay-weighting Use unweighted anomaly percentage
  --require-time-decay-policy Fail-fast when time-decay policy cannot be resolved
  --recurrence-tuning-policy-file Recurrence auto-tuning policy file
  --disable-recurrence-auto-tuning Disable recurrence-based half-life tuning
  --require-recurrence-tuning-policy Fail-fast when recurrence tuning policy cannot be resolved
  --seasonal-decomposition-policy-file Seasonal decomposition tuning policy file
  --reporting-cycle-length Reporting cycle length in samples (default: 12)
  --disable-seasonal-recurrence-decomposition Disable seasonal decomposition tuning
  --require-seasonal-decomposition-policy Fail-fast when seasonal decomposition policy cannot be resolved
  --seasonal-overlay-policy-file Seasonal overlay CSV for holiday/event multipliers
  --disable-seasonal-overlay-tuning Disable cross-cycle holiday/event overlay tuning
  --require-seasonal-overlay-policy Fail-fast when seasonal overlay policy cannot be resolved
  --causal-clustering-policy-file Causal clustering policy file
  --overlay-candidate-file Suggested overlay output markdown file
  --disable-causal-clustering Disable anomaly-causal clustering suggestions
  --require-causal-clustering-policy Fail-fast when causal clustering policy cannot be resolved
  --causal-graphing-policy-file Confidence-weighted causal graphing policy file
  --causal-graph-file Confidence-weighted causal graph markdown output
  --disable-confidence-causal-graphing Disable confidence-weighted causal graphing
  --require-causal-graphing-policy Fail-fast when causal graphing policy cannot be resolved
  --graph-cohort-window Causal graph cohort sample window (default: 30)
  --graph-temporal-decay-rate Temporal decay rate for graph edge persistence (default: 0.15)
  --disable-graph-edge-persistence Disable graph edge persistence scoring
  --signature-verification-mode Cryptographic mode: none|kms|sigstore
  --kms-public-key-file Public key file for kms signature verification
  --sigstore-certificate-file Fulcio certificate file for sigstore verify-blob
  --sigstore-certificate-identity Expected sigstore certificate identity
  --sigstore-oidc-issuer Expected sigstore OIDC issuer
  --cosign-path         Optional cosign binary path (default: cosign)
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
            MANIFEST_FILE_EXPLICIT=true
            shift 2
            ;;
        --require-signed-manifest)
            REQUIRE_SIGNED_MANIFEST=true
            shift
            ;;
        --trust-policy-file)
            TRUST_POLICY_FILE="${2:-}"
            shift 2
            ;;
        --require-trust-policy)
            REQUIRE_TRUST_POLICY=true
            shift
            ;;
        --revocation-file)
            REVOCATION_LIST_FILE="${2:-}"
            shift 2
            ;;
        --require-revocation-check)
            REQUIRE_REVOCATION_CHECK=true
            shift
            ;;
        --trust-policy-attestation-file)
            TRUST_POLICY_ATTESTATION_FILE="${2:-}"
            shift 2
            ;;
        --require-trust-policy-attestation)
            REQUIRE_TRUST_POLICY_ATTESTATION=true
            shift
            ;;
        --revocation-slo-file)
            REVOCATION_SLO_FILE="${2:-}"
            shift 2
            ;;
        --require-revocation-slo)
            REQUIRE_REVOCATION_SLO=true
            shift
            ;;
        --attestation-dashboard-file)
            ATTESTATION_DASHBOARD_FILE="${2:-}"
            shift 2
            ;;
        --attestation-history-file)
            ATTESTATION_TREND_HISTORY_FILE="${2:-}"
            shift 2
            ;;
        --attestation-baseline-window)
            ATTESTATION_BASELINE_WINDOW="${2:-}"
            shift 2
            ;;
        --attestation-anomaly-threshold-percent)
            ATTESTATION_ANOMALY_THRESHOLD_PERCENT="${2:-}"
            shift 2
            ;;
        --require-attestation-baseline-gate)
            REQUIRE_ATTESTATION_BASELINE_GATE=true
            shift
            ;;
        --disable-attestation-dashboard)
            ENABLE_ATTESTATION_DASHBOARD=false
            shift
            ;;
        --cross-tenant-heatmap-file)
            CROSS_TENANT_HEATMAP_FILE="${2:-}"
            shift 2
            ;;
        --auto-remediation-routing-file)
            AUTO_REMEDIATION_ROUTING_FILE="${2:-}"
            shift 2
            ;;
        --disable-cross-tenant-heatmap)
            ENABLE_CROSS_TENANT_HEATMAP=false
            shift
            ;;
        --tenant-criticality-file)
            TENANT_CRITICALITY_FILE="${2:-}"
            shift 2
            ;;
        --adaptive-threshold-policy-file)
            ADAPTIVE_THRESHOLD_POLICY_FILE="${2:-}"
            shift 2
            ;;
        --disable-adaptive-threshold-calibration)
            ENABLE_ADAPTIVE_THRESHOLD_CALIBRATION=false
            shift
            ;;
        --require-adaptive-threshold-policy)
            REQUIRE_ADAPTIVE_THRESHOLD_POLICY=true
            shift
            ;;
        --time-decay-policy-file)
            TIME_DECAY_POLICY_FILE="${2:-}"
            shift 2
            ;;
        --time-decay-half-life)
            TIME_DECAY_HALF_LIFE="${2:-}"
            shift 2
            ;;
        --disable-time-decay-weighting)
            ENABLE_TIME_DECAY_WEIGHTING=false
            shift
            ;;
        --require-time-decay-policy)
            REQUIRE_TIME_DECAY_POLICY=true
            shift
            ;;
        --recurrence-tuning-policy-file)
            RECURRENCE_TUNING_POLICY_FILE="${2:-}"
            shift 2
            ;;
        --disable-recurrence-auto-tuning)
            ENABLE_RECURRENCE_AUTO_TUNING=false
            shift
            ;;
        --require-recurrence-tuning-policy)
            REQUIRE_RECURRENCE_TUNING_POLICY=true
            shift
            ;;
        --seasonal-decomposition-policy-file)
            SEASONAL_DECOMPOSITION_POLICY_FILE="${2:-}"
            shift 2
            ;;
        --reporting-cycle-length)
            REPORTING_CYCLE_LENGTH="${2:-}"
            shift 2
            ;;
        --disable-seasonal-recurrence-decomposition)
            ENABLE_SEASONAL_RECURRENCE_DECOMPOSITION=false
            shift
            ;;
        --require-seasonal-decomposition-policy)
            REQUIRE_SEASONAL_DECOMPOSITION_POLICY=true
            shift
            ;;
        --seasonal-overlay-policy-file)
            SEASONAL_OVERLAY_POLICY_FILE="${2:-}"
            shift 2
            ;;
        --disable-seasonal-overlay-tuning)
            ENABLE_SEASONAL_OVERLAY_TUNING=false
            shift
            ;;
        --require-seasonal-overlay-policy)
            REQUIRE_SEASONAL_OVERLAY_POLICY=true
            shift
            ;;
        --causal-clustering-policy-file)
            CAUSAL_CLUSTERING_POLICY_FILE="${2:-}"
            shift 2
            ;;
        --overlay-candidate-file)
            OVERLAY_CANDIDATE_FILE="${2:-}"
            shift 2
            ;;
        --disable-causal-clustering)
            ENABLE_CAUSAL_CLUSTERING=false
            shift
            ;;
        --require-causal-clustering-policy)
            REQUIRE_CAUSAL_CLUSTERING_POLICY=true
            shift
            ;;
        --causal-graphing-policy-file)
            CAUSAL_GRAPHING_POLICY_FILE="${2:-}"
            shift 2
            ;;
        --causal-graph-file)
            CAUSAL_GRAPH_FILE="${2:-}"
            shift 2
            ;;
        --disable-confidence-causal-graphing)
            ENABLE_CONFIDENCE_CAUSAL_GRAPHING=false
            shift
            ;;
        --require-causal-graphing-policy)
            REQUIRE_CAUSAL_GRAPHING_POLICY=true
            shift
            ;;
        --graph-cohort-window)
            GRAPH_COHORT_WINDOW="${2:-}"
            shift 2
            ;;
        --graph-temporal-decay-rate)
            GRAPH_TEMPORAL_DECAY_RATE="${2:-}"
            shift 2
            ;;
        --disable-graph-edge-persistence)
            ENABLE_GRAPH_EDGE_PERSISTENCE=false
            shift
            ;;
        --signature-verification-mode)
            SIGNATURE_VERIFICATION_MODE="${2:-}"
            shift 2
            ;;
        --kms-public-key-file)
            KMS_PUBLIC_KEY_FILE="${2:-}"
            shift 2
            ;;
        --sigstore-certificate-file)
            SIGSTORE_CERTIFICATE_FILE="${2:-}"
            shift 2
            ;;
        --sigstore-certificate-identity)
            SIGSTORE_CERTIFICATE_IDENTITY="${2:-}"
            shift 2
            ;;
        --sigstore-oidc-issuer)
            SIGSTORE_OIDC_ISSUER="${2:-}"
            shift 2
            ;;
        --cosign-path)
            COSIGN_PATH="${2:-}"
            shift 2
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

if ! [[ "$ATTESTATION_BASELINE_WINDOW" =~ ^[0-9]+$ ]] || [ "$ATTESTATION_BASELINE_WINDOW" -le 0 ]; then
    echo "--attestation-baseline-window must be a positive integer" >&2
    exit 1
fi

if ! [[ "$ATTESTATION_ANOMALY_THRESHOLD_PERCENT" =~ ^[0-9]+$ ]] || [ "$ATTESTATION_ANOMALY_THRESHOLD_PERCENT" -lt 0 ] || [ "$ATTESTATION_ANOMALY_THRESHOLD_PERCENT" -gt 100 ]; then
    echo "--attestation-anomaly-threshold-percent must be an integer between 0 and 100" >&2
    exit 1
fi

if ! [[ "$TIME_DECAY_HALF_LIFE" =~ ^[0-9]+$ ]] || [ "$TIME_DECAY_HALF_LIFE" -le 0 ]; then
    echo "--time-decay-half-life must be a positive integer" >&2
    exit 1
fi

if ! [[ "$REPORTING_CYCLE_LENGTH" =~ ^[0-9]+$ ]] || [ "$REPORTING_CYCLE_LENGTH" -le 0 ]; then
    echo "--reporting-cycle-length must be a positive integer" >&2
    exit 1
fi

if ! [[ "$GRAPH_COHORT_WINDOW" =~ ^[0-9]+$ ]] || [ "$GRAPH_COHORT_WINDOW" -le 0 ]; then
    echo "--graph-cohort-window must be a positive integer" >&2
    exit 1
fi

if ! awk -v v="$GRAPH_TEMPORAL_DECAY_RATE" 'BEGIN { exit !(v+0>=0) }'; then
    echo "--graph-temporal-decay-rate must be a non-negative number" >&2
    exit 1
fi

if [ -z "$POLICY_PROFILE" ]; then
    echo "--policy-profile cannot be empty" >&2
    exit 1
fi

if [[ "$SIGNATURE_VERIFICATION_MODE" != "none" &&
      "$SIGNATURE_VERIFICATION_MODE" != "kms" &&
      "$SIGNATURE_VERIFICATION_MODE" != "sigstore" ]]; then
    echo "--signature-verification-mode must be one of: none, kms, sigstore" >&2
    exit 1
fi

if [ "$SIGNATURE_VERIFICATION_MODE" != "none" ]; then
    REQUIRE_SIGNED_MANIFEST=true
fi

if [ "$REQUIRE_TRUST_POLICY" = true ] || [ "$REQUIRE_REVOCATION_CHECK" = true ]; then
    REQUIRE_SIGNED_MANIFEST=true
fi

if [ "$REQUIRE_TRUST_POLICY_ATTESTATION" = true ] || [ "$REQUIRE_REVOCATION_SLO" = true ]; then
    REQUIRE_SIGNED_MANIFEST=true
fi

if [ ! -f "$TREND_HISTORY_FILE" ]; then
    printf "%s\n" "timestamp,tenant_id,rollout_phase,dependency,result,simulation_id" > "$TREND_HISTORY_FILE"
fi

if [ ! -f "$CAUSAL_HISTORY_FILE" ]; then
    printf "%s\n" "timestamp,tenant_id,rollout_phase,dependency,failure_mode,result,simulation_id" > "$CAUSAL_HISTORY_FILE"
fi

if [ "$ENABLE_ATTESTATION_DASHBOARD" = true ] && [ ! -f "$ATTESTATION_TREND_HISTORY_FILE" ]; then
    printf "%s\n" "timestamp,tenant_id,distribution_id,attestation_status,revocation_slo_status,slo_age_hours,simulation_id" > "$ATTESTATION_TREND_HISTORY_FILE"
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

if [ "$REQUIRE_SIGNED_MANIFEST" = true ] || [ "$MANIFEST_FILE_EXPLICIT" = true ]; then
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
    MANIFEST_SIGNATURE_SCHEME=$(awk -F'=' '$1=="signature_scheme" { print $2; exit }' "$PROFILE_MANIFEST_FILE" | tr -d '\r[:space:]')
    MANIFEST_SIGNATURE_FILE=$(awk -F'=' '$1=="signature_file" { print $2; exit }' "$PROFILE_MANIFEST_FILE" | tr -d '\r')
    MANIFEST_CERTIFICATE_FILE=$(awk -F'=' '$1=="certificate_file" { print $2; exit }' "$PROFILE_MANIFEST_FILE" | tr -d '\r')

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

    if [ "$SIGNATURE_VERIFICATION_MODE" != "none" ]; then
        if [ -z "$MANIFEST_SIGNATURE_SCHEME" ] || [ -z "$MANIFEST_SIGNATURE_FILE" ]; then
            echo "Malformed signed manifest: signature_scheme and signature_file are required for cryptographic verification" >&2
            exit 1
        fi

        if [ "$SIGNATURE_VERIFICATION_MODE" != "$MANIFEST_SIGNATURE_SCHEME" ]; then
            echo "Signature verification mode mismatch: requested '$SIGNATURE_VERIFICATION_MODE' but manifest declares '$MANIFEST_SIGNATURE_SCHEME'" >&2
            exit 1
        fi

        if [ ! -f "$MANIFEST_SIGNATURE_FILE" ]; then
            echo "Missing signature file referenced by manifest: $MANIFEST_SIGNATURE_FILE" >&2
            exit 1
        fi

        if [ "$SIGNATURE_VERIFICATION_MODE" = "kms" ]; then
            if [ -z "$KMS_PUBLIC_KEY_FILE" ]; then
                echo "--kms-public-key-file is required for --signature-verification-mode kms" >&2
                exit 1
            fi

            if [ ! -f "$KMS_PUBLIC_KEY_FILE" ]; then
                echo "Missing KMS public key file: $KMS_PUBLIC_KEY_FILE" >&2
                exit 1
            fi

            if ! command -v openssl >/dev/null 2>&1; then
                echo "Unable to verify kms signature: openssl not available" >&2
                exit 1
            fi

            if ! openssl dgst -sha256 -verify "$KMS_PUBLIC_KEY_FILE" -signature "$MANIFEST_SIGNATURE_FILE" "$PROFILE_WEIGHTS_FILE" >/dev/null 2>&1; then
                echo "Cryptographic signature verification failed for kms mode" >&2
                exit 1
            fi
        fi

        if [ "$SIGNATURE_VERIFICATION_MODE" = "sigstore" ]; then
            if [ -z "$SIGSTORE_CERTIFICATE_FILE" ] || [ -z "$SIGSTORE_CERTIFICATE_IDENTITY" ] || [ -z "$SIGSTORE_OIDC_ISSUER" ]; then
                echo "--sigstore-certificate-file, --sigstore-certificate-identity, and --sigstore-oidc-issuer are required for sigstore mode" >&2
                exit 1
            fi

            if [ ! -f "$SIGSTORE_CERTIFICATE_FILE" ]; then
                echo "Missing sigstore certificate file: $SIGSTORE_CERTIFICATE_FILE" >&2
                exit 1
            fi

            if ! command -v "$COSIGN_PATH" >/dev/null 2>&1; then
                echo "Unable to verify sigstore signature: cosign not available at '$COSIGN_PATH'" >&2
                exit 1
            fi

            if ! "$COSIGN_PATH" verify-blob --signature "$MANIFEST_SIGNATURE_FILE" --certificate "$SIGSTORE_CERTIFICATE_FILE" --certificate-identity "$SIGSTORE_CERTIFICATE_IDENTITY" --certificate-oidc-issuer "$SIGSTORE_OIDC_ISSUER" "$PROFILE_WEIGHTS_FILE" >/dev/null 2>&1; then
                echo "Cryptographic signature verification failed for sigstore mode" >&2
                exit 1
            fi
        fi
    fi

    if [ "$REQUIRE_TRUST_POLICY_ATTESTATION" = true ]; then
        if [ ! -f "$TRUST_POLICY_ATTESTATION_FILE" ]; then
            echo "Missing trust-policy attestation file: $TRUST_POLICY_ATTESTATION_FILE" >&2
            exit 1
        fi

        if [ ! -r "$TRUST_POLICY_ATTESTATION_FILE" ]; then
            echo "Trust-policy attestation file is not readable: $TRUST_POLICY_ATTESTATION_FILE" >&2
            exit 1
        fi

        ATTESTATION_DISTRIBUTION_ID=$(awk -F'=' '$1=="distribution_id" { print $2; exit }' "$TRUST_POLICY_ATTESTATION_FILE" | tr -d '\r[:space:]')
        ATTESTATION_PUBLISHED_AT=$(awk -F'=' '$1=="published_at" { print $2; exit }' "$TRUST_POLICY_ATTESTATION_FILE" | tr -d '\r[:space:]')
        ATTESTATION_STATUS=$(awk -F'=' '$1=="attestation_status" { print $2; exit }' "$TRUST_POLICY_ATTESTATION_FILE" | tr -d '\r[:space:]')
        ATTESTATION_SIGNER=$(awk -F'=' '$1=="signer" { print $2; exit }' "$TRUST_POLICY_ATTESTATION_FILE" | tr -d '\r[:space:]')
        ATTESTATION_ARTIFACT_DIGEST=$(awk -F'=' '$1=="artifact_digest" { print $2; exit }' "$TRUST_POLICY_ATTESTATION_FILE" | tr -d '\r[:space:]')

        if [ -z "$ATTESTATION_DISTRIBUTION_ID" ] || [ -z "$ATTESTATION_PUBLISHED_AT" ] || [ -z "$ATTESTATION_STATUS" ] || [ -z "$ATTESTATION_SIGNER" ] || [ -z "$ATTESTATION_ARTIFACT_DIGEST" ]; then
            echo "Malformed trust-policy attestation file: required fields missing in $TRUST_POLICY_ATTESTATION_FILE" >&2
            exit 1
        fi

        if [ "$ATTESTATION_STATUS" != "published" ] && [ "$ATTESTATION_STATUS" != "verified" ]; then
            echo "Trust-policy attestation status invalid: expected published|verified in $TRUST_POLICY_ATTESTATION_FILE" >&2
            exit 1
        fi

        if [ -n "$TRUST_POLICY_DISTRIBUTION_ID" ] && [ "$ATTESTATION_DISTRIBUTION_ID" != "$TRUST_POLICY_DISTRIBUTION_ID" ]; then
            echo "Trust-policy attestation mismatch: distribution_id '$ATTESTATION_DISTRIBUTION_ID' does not match trust policy '$TRUST_POLICY_DISTRIBUTION_ID'" >&2
            exit 1
        fi

        if ! [[ "$ATTESTATION_ARTIFACT_DIGEST" =~ ^[A-Fa-f0-9]{64}$ ]]; then
            echo "Malformed trust-policy attestation file: artifact_digest must be 64-char hex" >&2
            exit 1
        fi

        if command -v sha256sum >/dev/null 2>&1; then
            ACTUAL_TRUST_POLICY_SHA256=$(sha256sum "$TRUST_POLICY_FILE" | awk '{print $1}')
        elif command -v shasum >/dev/null 2>&1; then
            ACTUAL_TRUST_POLICY_SHA256=$(shasum -a 256 "$TRUST_POLICY_FILE" | awk '{print $1}')
        else
            echo "Unable to verify trust-policy attestation digest: sha256 tool not available" >&2
            exit 1
        fi

        if [ "${ACTUAL_TRUST_POLICY_SHA256,,}" != "${ATTESTATION_ARTIFACT_DIGEST,,}" ]; then
            echo "Trust-policy attestation digest mismatch for $TRUST_POLICY_FILE" >&2
            exit 1
        fi
    fi

    if [ "$REQUIRE_TRUST_POLICY" = true ]; then
        if [ ! -f "$TRUST_POLICY_FILE" ]; then
            echo "Missing trust-policy file: $TRUST_POLICY_FILE" >&2
            exit 1
        fi

        if [ ! -r "$TRUST_POLICY_FILE" ]; then
            echo "Trust-policy file is not readable: $TRUST_POLICY_FILE" >&2
            exit 1
        fi

        TRUST_POLICY_DISTRIBUTION_ID=$(awk -F'=' '$1=="distribution_id" { print $2; exit }' "$TRUST_POLICY_FILE" | tr -d '\r[:space:]')
        TRUST_POLICY_DISTRIBUTED_AT=$(awk -F'=' '$1=="distributed_at" { print $2; exit }' "$TRUST_POLICY_FILE" | tr -d '\r[:space:]')
        TRUST_POLICY_FEDERATION_SCOPE=$(awk -F'=' '$1=="federation_scope" { print $2; exit }' "$TRUST_POLICY_FILE" | tr -d '\r[:space:]')
        TRUST_POLICY_ACTIVE_KEYS=$(awk -F'=' '$1=="active_key_ids" { print $2; exit }' "$TRUST_POLICY_FILE" | tr -d '\r[:space:]')
        TRUST_POLICY_REVOKED_KEYS=$(awk -F'=' '$1=="revoked_key_ids" { print $2; exit }' "$TRUST_POLICY_FILE" | tr -d '\r[:space:]')
        TRUST_POLICY_MAX_AGE_DAYS=$(awk -F'=' '$1=="max_distribution_age_days" { print $2; exit }' "$TRUST_POLICY_FILE" | tr -d '\r[:space:]')

        if [ -z "$TRUST_POLICY_DISTRIBUTION_ID" ] || [ -z "$TRUST_POLICY_DISTRIBUTED_AT" ] || [ -z "$TRUST_POLICY_FEDERATION_SCOPE" ] || [ -z "$TRUST_POLICY_ACTIVE_KEYS" ]; then
            echo "Malformed trust-policy file: required fields missing in $TRUST_POLICY_FILE" >&2
            exit 1
        fi

        if [ -n "$TRUST_POLICY_MAX_AGE_DAYS" ] && ! [[ "$TRUST_POLICY_MAX_AGE_DAYS" =~ ^[0-9]+$ ]]; then
            echo "Malformed trust-policy file: max_distribution_age_days must be numeric in $TRUST_POLICY_FILE" >&2
            exit 1
        fi

        ACTIVE_KEYS_NORMALIZED=$(echo "$TRUST_POLICY_ACTIVE_KEYS" | tr ',' ';')
        REVOKED_KEYS_NORMALIZED=$(echo "$TRUST_POLICY_REVOKED_KEYS" | tr ',' ';')

        if [[ ";$ACTIVE_KEYS_NORMALIZED;" != *";$MANIFEST_KEY_ID;"* ]]; then
            echo "Trust-policy violation: key_id '$MANIFEST_KEY_ID' is not active in $TRUST_POLICY_FILE" >&2
            exit 1
        fi

        if [ -n "$REVOKED_KEYS_NORMALIZED" ] && [[ ";$REVOKED_KEYS_NORMALIZED;" == *";$MANIFEST_KEY_ID;"* ]]; then
            echo "Trust-policy violation: key_id '$MANIFEST_KEY_ID' is explicitly revoked in $TRUST_POLICY_FILE" >&2
            exit 1
        fi

        TRUST_POLICY_ACTIVE_KEY_COUNT=$(printf "%s" "$ACTIVE_KEYS_NORMALIZED" | awk -F';' '{ c=0; for (i=1; i<=NF; i++) if ($i!="") c++; print c }')
        TRUST_POLICY_REVOKED_KEY_COUNT=$(printf "%s" "$REVOKED_KEYS_NORMALIZED" | awk -F';' '{ c=0; for (i=1; i<=NF; i++) if ($i!="") c++; print c }')

        if [ -n "$TRUST_POLICY_MAX_AGE_DAYS" ] && [ "$TRUST_POLICY_MAX_AGE_DAYS" -gt 0 ]; then
            if ! date -u -d "$TRUST_POLICY_DISTRIBUTED_AT" +%s >/dev/null 2>&1; then
                echo "Malformed trust-policy file: distributed_at is not RFC3339-compatible in $TRUST_POLICY_FILE" >&2
                exit 1
            fi

            DISTRIBUTED_EPOCH=$(date -u -d "$TRUST_POLICY_DISTRIBUTED_AT" +%s)
            NOW_EPOCH=$(date -u +%s)
            TRUST_POLICY_AGE_DAYS=$(( (NOW_EPOCH - DISTRIBUTED_EPOCH) / 86400 ))

            if [ "$TRUST_POLICY_AGE_DAYS" -gt "$TRUST_POLICY_MAX_AGE_DAYS" ]; then
                echo "Trust-policy distribution is stale: age $TRUST_POLICY_AGE_DAYS days exceeds $TRUST_POLICY_MAX_AGE_DAYS in $TRUST_POLICY_FILE" >&2
                exit 1
            fi
        fi
    fi

    if [ "$REQUIRE_REVOCATION_CHECK" = true ]; then
        if [ ! -f "$REVOCATION_LIST_FILE" ]; then
            echo "Missing revocation list file: $REVOCATION_LIST_FILE" >&2
            exit 1
        fi

        if [ ! -r "$REVOCATION_LIST_FILE" ]; then
            echo "Revocation list file is not readable: $REVOCATION_LIST_FILE" >&2
            exit 1
        fi

        REVOKED_ROW=$(awk -F',' -v key="$MANIFEST_KEY_ID" 'NR > 1 && $1 == key && tolower($2) == "revoked" { print $0; exit }' "$REVOCATION_LIST_FILE")
        if [ -n "$REVOKED_ROW" ]; then
            echo "Revocation check failed: key_id '$MANIFEST_KEY_ID' is revoked in $REVOCATION_LIST_FILE" >&2
            exit 1
        fi
    fi

    if [ "$REQUIRE_REVOCATION_SLO" = true ]; then
        if [ ! -f "$REVOCATION_SLO_FILE" ]; then
            echo "Missing revocation SLO file: $REVOCATION_SLO_FILE" >&2
            exit 1
        fi

        if [ ! -r "$REVOCATION_SLO_FILE" ]; then
            echo "Revocation SLO file is not readable: $REVOCATION_SLO_FILE" >&2
            exit 1
        fi

        REVOCATION_SLO_MAX_HOURS=$(awk -F'=' '$1=="max_propagation_hours" { print $2; exit }' "$REVOCATION_SLO_FILE" | tr -d '\r[:space:]')
        REVOCATION_SLO_LAST_CHECK_AT=$(awk -F'=' '$1=="last_propagation_check_at" { print $2; exit }' "$REVOCATION_SLO_FILE" | tr -d '\r[:space:]')
        REVOCATION_SLO_STATUS=$(awk -F'=' '$1=="status" { print $2; exit }' "$REVOCATION_SLO_FILE" | tr -d '\r[:space:]')

        if [ -z "$REVOCATION_SLO_MAX_HOURS" ] || [ -z "$REVOCATION_SLO_LAST_CHECK_AT" ] || [ -z "$REVOCATION_SLO_STATUS" ]; then
            echo "Malformed revocation SLO file: required fields missing in $REVOCATION_SLO_FILE" >&2
            exit 1
        fi

        if ! [[ "$REVOCATION_SLO_MAX_HOURS" =~ ^[0-9]+$ ]]; then
            echo "Malformed revocation SLO file: max_propagation_hours must be numeric in $REVOCATION_SLO_FILE" >&2
            exit 1
        fi

        if [ "$REVOCATION_SLO_STATUS" != "healthy" ] && [ "$REVOCATION_SLO_STATUS" != "warning" ]; then
            echo "Revocation SLO status invalid: expected healthy|warning in $REVOCATION_SLO_FILE" >&2
            exit 1
        fi

        if ! date -u -d "$REVOCATION_SLO_LAST_CHECK_AT" +%s >/dev/null 2>&1; then
            echo "Malformed revocation SLO file: last_propagation_check_at is not RFC3339-compatible in $REVOCATION_SLO_FILE" >&2
            exit 1
        fi

        LAST_CHECK_EPOCH=$(date -u -d "$REVOCATION_SLO_LAST_CHECK_AT" +%s)
        NOW_EPOCH=$(date -u +%s)
        SLO_AGE_HOURS=$(( (NOW_EPOCH - LAST_CHECK_EPOCH) / 3600 ))

        if [ "$SLO_AGE_HOURS" -gt "$REVOCATION_SLO_MAX_HOURS" ]; then
            echo "Revocation propagation SLO breach: age $SLO_AGE_HOURS hours exceeds $REVOCATION_SLO_MAX_HOURS in $REVOCATION_SLO_FILE" >&2
            exit 1
        fi
    fi

    if [ "$ENABLE_ATTESTATION_DASHBOARD" = true ]; then
        if [ ! -f "$ATTESTATION_TREND_HISTORY_FILE" ]; then
            printf "%s\n" "timestamp,tenant_id,distribution_id,attestation_status,revocation_slo_status,slo_age_hours,simulation_id" > "$ATTESTATION_TREND_HISTORY_FILE"
        fi

        ATTESTATION_STATUS_VALUE="${ATTESTATION_STATUS:-unknown}"
        SLO_STATUS_VALUE="${REVOCATION_SLO_STATUS:-unknown}"
        SLO_AGE_VALUE="${SLO_AGE_HOURS:-0}"
        DISTRIBUTION_VALUE="${ATTESTATION_DISTRIBUTION_ID:-${TRUST_POLICY_DISTRIBUTION_ID:-unknown}}"

        TENANT_ATTESTATION_HISTORY=$(awk -F',' -v tenant="$TENANT_ID" 'NR > 1 && $2 == tenant { print $0 }' "$ATTESTATION_TREND_HISTORY_FILE" | tail -n "$ATTESTATION_BASELINE_WINDOW")
        ATTESTATION_BASELINE_TOTAL=$(printf "%s\n" "$TENANT_ATTESTATION_HISTORY" | sed '/^$/d' | wc -l | tr -d ' ')
        ATTESTATION_BASELINE_ALERT_COUNT=$(printf "%s\n" "$TENANT_ATTESTATION_HISTORY" | awk -F',' 'NF > 0 && ($4 != "verified" || $5 == "warning") { c++ } END { print c + 0 }')

        ATTESTATION_ANOMALY_PERCENT=0
        if [ "$ATTESTATION_BASELINE_TOTAL" -gt 0 ]; then
            ATTESTATION_ANOMALY_PERCENT=$(( ATTESTATION_BASELINE_ALERT_COUNT * 100 / ATTESTATION_BASELINE_TOTAL ))
        fi

        WEIGHTED_ALERT_SUM=0
        WEIGHTED_TOTAL_SUM=0
        WEIGHTED_ANOMALY_PERCENT="$ATTESTATION_ANOMALY_PERCENT"
        TIME_DECAY_SOURCE="disabled"
        RECURRENCE_RATIO_PERCENT=0
        RECURRENCE_TUNED_HALF_LIFE="$TIME_DECAY_HALF_LIFE"
        RECURRENCE_TUNING_SOURCE="disabled"
        SEASONAL_PHASE="not-applied"
        SEASONAL_MULTIPLIER=1.0
        SEASONAL_TUNED_HALF_LIFE="$TIME_DECAY_HALF_LIFE"
        SEASONAL_DECOMPOSITION_SOURCE="disabled"
        SEASONAL_OVERLAY_NAME="none"
        SEASONAL_OVERLAY_MULTIPLIER=1.0
        SEASONAL_OVERLAY_SOURCE="disabled"
        CAUSAL_CLUSTER_KEY="none"
        CAUSAL_CLUSTER_SAMPLE_COUNT=0
        CAUSAL_CLUSTER_FAIL_COUNT=0
        CAUSAL_CLUSTER_FAIL_PERCENT=0
        CAUSAL_CLUSTER_SUGGESTED_OVERLAY="none"
        CAUSAL_CLUSTER_SUGGESTED_MULTIPLIER=1.00
        CAUSAL_CLUSTER_SOURCE="disabled"
        CAUSAL_CONFIDENCE_SCORE=0
        CAUSAL_CONFIDENCE_THRESHOLD=60
        CAUSAL_CONFIDENCE_SOURCE="disabled"
        CAUSAL_GRAPH_NODE="none"
        CAUSAL_GRAPH_EDGE="none"

        if [ "$ENABLE_TIME_DECAY_WEIGHTING" = true ]; then
            TIME_DECAY_SOURCE="cli"
            DECAY_HALF_LIFE_EFFECTIVE="$TIME_DECAY_HALF_LIFE"

            if [ "$ENABLE_RECURRENCE_AUTO_TUNING" = true ]; then
                RECURRENCE_TUNING_SOURCE="static"
                RECURRENCE_TUNING_MIN=2
                RECURRENCE_TUNING_MAX=20
                RECURRENCE_TUNING_FAIL_MULTIPLIER=2.0

                if [ -f "$RECURRENCE_TUNING_POLICY_FILE" ] && [ -r "$RECURRENCE_TUNING_POLICY_FILE" ]; then
                    POLICY_MIN=$(awk -F'=' '$1=="min_half_life_samples" { print $2; exit }' "$RECURRENCE_TUNING_POLICY_FILE" | tr -d '\r[:space:]')
                    POLICY_MAX=$(awk -F'=' '$1=="max_half_life_samples" { print $2; exit }' "$RECURRENCE_TUNING_POLICY_FILE" | tr -d '\r[:space:]')
                    POLICY_MULT=$(awk -F'=' '$1=="fail_multiplier" { print $2; exit }' "$RECURRENCE_TUNING_POLICY_FILE" | tr -d '\r[:space:]')

                    if [ -n "$POLICY_MIN" ] && [[ "$POLICY_MIN" =~ ^[0-9]+$ ]] && [ "$POLICY_MIN" -gt 0 ]; then
                        RECURRENCE_TUNING_MIN="$POLICY_MIN"
                    elif [ "$REQUIRE_RECURRENCE_TUNING_POLICY" = true ]; then
                        echo "Recurrence tuning policy failed: min_half_life_samples missing/invalid in $RECURRENCE_TUNING_POLICY_FILE" >&2
                        exit 1
                    fi

                    if [ -n "$POLICY_MAX" ] && [[ "$POLICY_MAX" =~ ^[0-9]+$ ]] && [ "$POLICY_MAX" -ge "$RECURRENCE_TUNING_MIN" ]; then
                        RECURRENCE_TUNING_MAX="$POLICY_MAX"
                    elif [ "$REQUIRE_RECURRENCE_TUNING_POLICY" = true ]; then
                        echo "Recurrence tuning policy failed: max_half_life_samples missing/invalid in $RECURRENCE_TUNING_POLICY_FILE" >&2
                        exit 1
                    fi

                    if [ -n "$POLICY_MULT" ] && awk -v v="$POLICY_MULT" 'BEGIN { exit !(v+0>0) }'; then
                        RECURRENCE_TUNING_FAIL_MULTIPLIER="$POLICY_MULT"
                    elif [ "$REQUIRE_RECURRENCE_TUNING_POLICY" = true ]; then
                        echo "Recurrence tuning policy failed: fail_multiplier missing/invalid in $RECURRENCE_TUNING_POLICY_FILE" >&2
                        exit 1
                    fi

                    RECURRENCE_TUNING_SOURCE="policy"
                elif [ "$REQUIRE_RECURRENCE_TUNING_POLICY" = true ]; then
                    echo "Recurrence tuning policy failed: missing file $RECURRENCE_TUNING_POLICY_FILE" >&2
                    exit 1
                fi

                FAIL_COUNT=$(printf "%s\n" "$TENANT_ATTESTATION_HISTORY" | awk -F',' 'NF>0 && ($4!="verified" || $5=="warning") { c++ } END { print c + 0 }')
                if [ "$ATTESTATION_BASELINE_TOTAL" -gt 0 ]; then
                    RECURRENCE_RATIO_PERCENT=$(( FAIL_COUNT * 100 / ATTESTATION_BASELINE_TOTAL ))
                fi

                TUNED=$(awk -v base="$DECAY_HALF_LIFE_EFFECTIVE" -v ratio="$RECURRENCE_RATIO_PERCENT" -v mult="$RECURRENCE_TUNING_FAIL_MULTIPLIER" -v minv="$RECURRENCE_TUNING_MIN" -v maxv="$RECURRENCE_TUNING_MAX" 'BEGIN { v = base + ((ratio/100.0) * mult * base); if (v < minv) v=minv; if (v > maxv) v=maxv; printf "%d", v }')
                DECAY_HALF_LIFE_EFFECTIVE="$TUNED"
                RECURRENCE_TUNED_HALF_LIFE="$TUNED"
            fi

            if [ -f "$TIME_DECAY_POLICY_FILE" ] && [ -r "$TIME_DECAY_POLICY_FILE" ]; then
                POLICY_HALF_LIFE=$(awk -F'=' '$1=="half_life_samples" { print $2; exit }' "$TIME_DECAY_POLICY_FILE" | tr -d '\r[:space:]')
                if [ -n "$POLICY_HALF_LIFE" ] && [[ "$POLICY_HALF_LIFE" =~ ^[0-9]+$ ]] && [ "$POLICY_HALF_LIFE" -gt 0 ]; then
                    DECAY_HALF_LIFE_EFFECTIVE="$POLICY_HALF_LIFE"
                    TIME_DECAY_SOURCE="policy"
                elif [ "$REQUIRE_TIME_DECAY_POLICY" = true ]; then
                    echo "Time-decay policy failed: half_life_samples missing or invalid in $TIME_DECAY_POLICY_FILE" >&2
                    exit 1
                fi
            elif [ "$REQUIRE_TIME_DECAY_POLICY" = true ]; then
                echo "Time-decay policy failed: missing file $TIME_DECAY_POLICY_FILE" >&2
                exit 1
            fi

            if [ "$ENABLE_RECURRENCE_AUTO_TUNING" = true ]; then
                TUNED=$(awk -v base="$DECAY_HALF_LIFE_EFFECTIVE" -v ratio="$RECURRENCE_RATIO_PERCENT" -v mult="$RECURRENCE_TUNING_FAIL_MULTIPLIER" -v minv="$RECURRENCE_TUNING_MIN" -v maxv="$RECURRENCE_TUNING_MAX" 'BEGIN { v = base + ((ratio/100.0) * mult * base); if (v < minv) v=minv; if (v > maxv) v=maxv; printf "%d", v }')
                DECAY_HALF_LIFE_EFFECTIVE="$TUNED"
                RECURRENCE_TUNED_HALF_LIFE="$TUNED"
            fi

            if [ "$ENABLE_SEASONAL_RECURRENCE_DECOMPOSITION" = true ]; then
                SEASONAL_DECOMPOSITION_SOURCE="static"
                CYCLE_LENGTH_EFFECTIVE="$REPORTING_CYCLE_LENGTH"
                SEASONAL_START_MULT=1.20
                SEASONAL_MID_MULT=1.00
                SEASONAL_END_MULT=0.85
                SEASONAL_MIN_HALF_LIFE=2
                SEASONAL_MAX_HALF_LIFE=30

                if [ -f "$SEASONAL_DECOMPOSITION_POLICY_FILE" ] && [ -r "$SEASONAL_DECOMPOSITION_POLICY_FILE" ]; then
                    POLICY_CYCLE=$(awk -F'=' '$1=="reporting_cycle_length" { print $2; exit }' "$SEASONAL_DECOMPOSITION_POLICY_FILE" | tr -d '\r[:space:]')
                    POLICY_START=$(awk -F'=' '$1=="cycle_start_multiplier" { print $2; exit }' "$SEASONAL_DECOMPOSITION_POLICY_FILE" | tr -d '\r[:space:]')
                    POLICY_MID=$(awk -F'=' '$1=="cycle_mid_multiplier" { print $2; exit }' "$SEASONAL_DECOMPOSITION_POLICY_FILE" | tr -d '\r[:space:]')
                    POLICY_END=$(awk -F'=' '$1=="cycle_end_multiplier" { print $2; exit }' "$SEASONAL_DECOMPOSITION_POLICY_FILE" | tr -d '\r[:space:]')
                    POLICY_MIN_HALF=$(awk -F'=' '$1=="seasonal_min_half_life_samples" { print $2; exit }' "$SEASONAL_DECOMPOSITION_POLICY_FILE" | tr -d '\r[:space:]')
                    POLICY_MAX_HALF=$(awk -F'=' '$1=="seasonal_max_half_life_samples" { print $2; exit }' "$SEASONAL_DECOMPOSITION_POLICY_FILE" | tr -d '\r[:space:]')

                    [ -n "$POLICY_CYCLE" ] && [[ "$POLICY_CYCLE" =~ ^[0-9]+$ ]] && [ "$POLICY_CYCLE" -gt 0 ] && CYCLE_LENGTH_EFFECTIVE="$POLICY_CYCLE"
                    [ -n "$POLICY_START" ] && awk -v v="$POLICY_START" 'BEGIN { exit !(v+0>0) }' && SEASONAL_START_MULT="$POLICY_START"
                    [ -n "$POLICY_MID" ] && awk -v v="$POLICY_MID" 'BEGIN { exit !(v+0>0) }' && SEASONAL_MID_MULT="$POLICY_MID"
                    [ -n "$POLICY_END" ] && awk -v v="$POLICY_END" 'BEGIN { exit !(v+0>0) }' && SEASONAL_END_MULT="$POLICY_END"
                    [ -n "$POLICY_MIN_HALF" ] && [[ "$POLICY_MIN_HALF" =~ ^[0-9]+$ ]] && [ "$POLICY_MIN_HALF" -gt 0 ] && SEASONAL_MIN_HALF_LIFE="$POLICY_MIN_HALF"
                    [ -n "$POLICY_MAX_HALF" ] && [[ "$POLICY_MAX_HALF" =~ ^[0-9]+$ ]] && [ "$POLICY_MAX_HALF" -ge "$SEASONAL_MIN_HALF_LIFE" ] && SEASONAL_MAX_HALF_LIFE="$POLICY_MAX_HALF"
                    SEASONAL_DECOMPOSITION_SOURCE="policy"
                elif [ "$REQUIRE_SEASONAL_DECOMPOSITION_POLICY" = true ]; then
                    echo "Seasonal decomposition policy failed: missing file $SEASONAL_DECOMPOSITION_POLICY_FILE" >&2
                    exit 1
                fi

                CYCLE_POSITION=$(( ATTESTATION_BASELINE_TOTAL % CYCLE_LENGTH_EFFECTIVE ))
                CYCLE_THIRD=$(( CYCLE_LENGTH_EFFECTIVE / 3 ))
                [ "$CYCLE_THIRD" -le 0 ] && CYCLE_THIRD=1
                if [ "$CYCLE_POSITION" -lt "$CYCLE_THIRD" ]; then
                    SEASONAL_PHASE="cycle_start"
                    SEASONAL_MULTIPLIER="$SEASONAL_START_MULT"
                elif [ "$CYCLE_POSITION" -lt $(( CYCLE_THIRD * 2 )) ]; then
                    SEASONAL_PHASE="cycle_mid"
                    SEASONAL_MULTIPLIER="$SEASONAL_MID_MULT"
                else
                    SEASONAL_PHASE="cycle_end"
                    SEASONAL_MULTIPLIER="$SEASONAL_END_MULT"
                fi

                if [ "$ENABLE_SEASONAL_OVERLAY_TUNING" = true ]; then
                    SEASONAL_OVERLAY_SOURCE="static"
                    if [ -f "$SEASONAL_OVERLAY_POLICY_FILE" ] && [ -r "$SEASONAL_OVERLAY_POLICY_FILE" ]; then
                        OVERLAY_ROW=$(awk -F',' -v phase="$SEASONAL_PHASE" -v idx="$CYCLE_POSITION" 'NR>1 && (($1==phase || $1=="*") && ($2==idx || $2=="*")) { print $0; exit }' "$SEASONAL_OVERLAY_POLICY_FILE")
                        if [ -n "$OVERLAY_ROW" ]; then
                            OVERLAY_NAME=$(printf "%s" "$OVERLAY_ROW" | awk -F',' '{print $3}' | tr -d '\r')
                            OVERLAY_MULT=$(printf "%s" "$OVERLAY_ROW" | awk -F',' '{print $4}' | tr -d '\r[:space:]')
                            if [ -n "$OVERLAY_MULT" ] && awk -v v="$OVERLAY_MULT" 'BEGIN { exit !(v+0>0) }'; then
                                SEASONAL_OVERLAY_NAME="${OVERLAY_NAME:-unnamed-overlay}"
                                SEASONAL_OVERLAY_MULTIPLIER="$OVERLAY_MULT"
                                SEASONAL_OVERLAY_SOURCE="policy"
                                SEASONAL_MULTIPLIER=$(awk -v a="$SEASONAL_MULTIPLIER" -v b="$SEASONAL_OVERLAY_MULTIPLIER" 'BEGIN { printf "%.6f", a*b }')
                            elif [ "$REQUIRE_SEASONAL_OVERLAY_POLICY" = true ]; then
                                echo "Seasonal overlay policy failed: invalid overlay multiplier in $SEASONAL_OVERLAY_POLICY_FILE" >&2
                                exit 1
                            fi
                        elif [ "$REQUIRE_SEASONAL_OVERLAY_POLICY" = true ]; then
                            echo "Seasonal overlay policy failed: no overlay match for phase=$SEASONAL_PHASE cycle_index=$CYCLE_POSITION in $SEASONAL_OVERLAY_POLICY_FILE" >&2
                            exit 1
                        fi
                    elif [ "$REQUIRE_SEASONAL_OVERLAY_POLICY" = true ]; then
                        echo "Seasonal overlay policy failed: missing file $SEASONAL_OVERLAY_POLICY_FILE" >&2
                        exit 1
                    fi
                fi

                SEASONAL_TUNED_HALF_LIFE=$(awk -v base="$DECAY_HALF_LIFE_EFFECTIVE" -v mult="$SEASONAL_MULTIPLIER" -v minv="$SEASONAL_MIN_HALF_LIFE" -v maxv="$SEASONAL_MAX_HALF_LIFE" 'BEGIN { v = base * mult; if (v < minv) v=minv; if (v > maxv) v=maxv; printf "%d", v }')
                DECAY_HALF_LIFE_EFFECTIVE="$SEASONAL_TUNED_HALF_LIFE"
            fi

            if [ "$ATTESTATION_BASELINE_TOTAL" -gt 0 ]; then
                INDEX=0
                while IFS=',' read -r ts tenant dist attStatus sloStatus sloAge sim; do
                    [ -z "$tenant" ] && continue
                    INDEX=$((INDEX + 1))
                    AGE=$((ATTESTATION_BASELINE_TOTAL - INDEX))
                    WEIGHT=$(awk -v age="$AGE" -v hl="$DECAY_HALF_LIFE_EFFECTIVE" 'BEGIN { if (hl<=0) { print 1 } else { printf "%.6f", exp(log(0.5)*age/hl) } }')
                    WEIGHTED_TOTAL_SUM=$(awk -v a="$WEIGHTED_TOTAL_SUM" -v b="$WEIGHT" 'BEGIN { printf "%.6f", a + b }')

                    if [ "$attStatus" != "verified" ] || [ "$sloStatus" = "warning" ]; then
                        WEIGHTED_ALERT_SUM=$(awk -v a="$WEIGHTED_ALERT_SUM" -v b="$WEIGHT" 'BEGIN { printf "%.6f", a + b }')
                    fi
                done <<< "$TENANT_ATTESTATION_HISTORY"

                if awk -v total="$WEIGHTED_TOTAL_SUM" 'BEGIN { exit !(total > 0) }'; then
                    WEIGHTED_ANOMALY_PERCENT=$(awk -v alert="$WEIGHTED_ALERT_SUM" -v total="$WEIGHTED_TOTAL_SUM" 'BEGIN { printf "%d", (alert*100)/total }')
                fi
            fi

            if [ "$ENABLE_CAUSAL_CLUSTERING" = true ]; then
                CAUSAL_CLUSTER_SOURCE="static"
                CLUSTER_MIN_SAMPLES=3
                CLUSTER_FAIL_THRESHOLD_PERCENT=50
                CLUSTER_HIGH_MULTIPLIER=1.15

                if [ -f "$CAUSAL_CLUSTERING_POLICY_FILE" ] && [ -r "$CAUSAL_CLUSTERING_POLICY_FILE" ]; then
                    P_MIN=$(awk -F'=' '$1=="min_samples" { print $2; exit }' "$CAUSAL_CLUSTERING_POLICY_FILE" | tr -d '\r[:space:]')
                    P_FAIL=$(awk -F'=' '$1=="fail_threshold_percent" { print $2; exit }' "$CAUSAL_CLUSTERING_POLICY_FILE" | tr -d '\r[:space:]')
                    P_MULT=$(awk -F'=' '$1=="high_impact_multiplier" { print $2; exit }' "$CAUSAL_CLUSTERING_POLICY_FILE" | tr -d '\r[:space:]')
                    [ -n "$P_MIN" ] && [[ "$P_MIN" =~ ^[0-9]+$ ]] && [ "$P_MIN" -gt 0 ] && CLUSTER_MIN_SAMPLES="$P_MIN"
                    [ -n "$P_FAIL" ] && [[ "$P_FAIL" =~ ^[0-9]+$ ]] && [ "$P_FAIL" -ge 0 ] && [ "$P_FAIL" -le 100 ] && CLUSTER_FAIL_THRESHOLD_PERCENT="$P_FAIL"
                    [ -n "$P_MULT" ] && awk -v v="$P_MULT" 'BEGIN { exit !(v+0>0) }' && CLUSTER_HIGH_MULTIPLIER="$P_MULT"
                    CAUSAL_CLUSTER_SOURCE="policy"
                elif [ "$REQUIRE_CAUSAL_CLUSTERING_POLICY" = true ]; then
                    echo "Causal clustering policy failed: missing file $CAUSAL_CLUSTERING_POLICY_FILE" >&2
                    exit 1
                fi

                EFFECTIVE_FAILURE_MODE="${FAILURE_MODE:-unspecified}"
                CAUSAL_CLUSTER_KEY="$DEPENDENCY|$ROLLOUT_PHASE|$EFFECTIVE_FAILURE_MODE"
                CLUSTER_ROWS=$(awk -F',' -v tenant="$TENANT_ID" -v dep="$DEPENDENCY" -v phase="$ROLLOUT_PHASE" -v mode="$EFFECTIVE_FAILURE_MODE" 'NR>1 && $2==tenant && $4==dep && $3==phase && $5==mode { print $0 }' "$CAUSAL_HISTORY_FILE" | tail -n "$ATTESTATION_BASELINE_WINDOW")
                CAUSAL_CLUSTER_SAMPLE_COUNT=$(printf "%s\n" "$CLUSTER_ROWS" | sed '/^$/d' | wc -l | tr -d ' ')
                CAUSAL_CLUSTER_FAIL_COUNT=$(printf "%s\n" "$CLUSTER_ROWS" | awk -F',' 'NF>0 && $6=="fail" { c++ } END { print c + 0 }')
                if [ "$CAUSAL_CLUSTER_SAMPLE_COUNT" -gt 0 ]; then
                    CAUSAL_CLUSTER_FAIL_PERCENT=$(( CAUSAL_CLUSTER_FAIL_COUNT * 100 / CAUSAL_CLUSTER_SAMPLE_COUNT ))
                fi

                if [ "$CAUSAL_CLUSTER_SAMPLE_COUNT" -ge "$CLUSTER_MIN_SAMPLES" ] && [ "$CAUSAL_CLUSTER_FAIL_PERCENT" -ge "$CLUSTER_FAIL_THRESHOLD_PERCENT" ]; then
                    CAUSAL_CLUSTER_SUGGESTED_OVERLAY="suggested-$DEPENDENCY-$ROLLOUT_PHASE"
                    CAUSAL_CLUSTER_SUGGESTED_MULTIPLIER="$CLUSTER_HIGH_MULTIPLIER"
                fi

                if [ "$ENABLE_CONFIDENCE_CAUSAL_GRAPHING" = true ]; then
                    CAUSAL_CONFIDENCE_SOURCE="static"
                    CONF_SAMPLE_WEIGHT=0.4
                    CONF_FAIL_WEIGHT=0.5
                    CONF_CONTEXT_WEIGHT=0.1
                    CONF_PERSIST_WEIGHT=0.2
                    CAUSAL_CONFIDENCE_THRESHOLD=60
                    EDGE_PERSISTENCE_PERCENT=0
                    EDGE_PERSISTENCE_SCORE=0
                    GRAPH_TEMPORAL_SOURCE="static"

                    if [ -f "$CAUSAL_GRAPHING_POLICY_FILE" ] && [ -r "$CAUSAL_GRAPHING_POLICY_FILE" ]; then
                        P_SW=$(awk -F'=' '$1=="sample_weight" { print $2; exit }' "$CAUSAL_GRAPHING_POLICY_FILE" | tr -d '\r[:space:]')
                        P_FW=$(awk -F'=' '$1=="fail_weight" { print $2; exit }' "$CAUSAL_GRAPHING_POLICY_FILE" | tr -d '\r[:space:]')
                        P_CW=$(awk -F'=' '$1=="context_weight" { print $2; exit }' "$CAUSAL_GRAPHING_POLICY_FILE" | tr -d '\r[:space:]')
                        P_PW=$(awk -F'=' '$1=="edge_persistence_weight" { print $2; exit }' "$CAUSAL_GRAPHING_POLICY_FILE" | tr -d '\r[:space:]')
                        P_TH=$(awk -F'=' '$1=="recommendation_threshold" { print $2; exit }' "$CAUSAL_GRAPHING_POLICY_FILE" | tr -d '\r[:space:]')
                        P_GW=$(awk -F'=' '$1=="cohort_window" { print $2; exit }' "$CAUSAL_GRAPHING_POLICY_FILE" | tr -d '\r[:space:]')
                        P_GD=$(awk -F'=' '$1=="temporal_decay_rate" { print $2; exit }' "$CAUSAL_GRAPHING_POLICY_FILE" | tr -d '\r[:space:]')

                        [ -n "$P_SW" ] && awk -v v="$P_SW" 'BEGIN { exit !(v+0>=0) }' && CONF_SAMPLE_WEIGHT="$P_SW"
                        [ -n "$P_FW" ] && awk -v v="$P_FW" 'BEGIN { exit !(v+0>=0) }' && CONF_FAIL_WEIGHT="$P_FW"
                        [ -n "$P_CW" ] && awk -v v="$P_CW" 'BEGIN { exit !(v+0>=0) }' && CONF_CONTEXT_WEIGHT="$P_CW"
                        [ -n "$P_PW" ] && awk -v v="$P_PW" 'BEGIN { exit !(v+0>=0) }' && CONF_PERSIST_WEIGHT="$P_PW"
                        [ -n "$P_TH" ] && [[ "$P_TH" =~ ^[0-9]+$ ]] && [ "$P_TH" -ge 0 ] && [ "$P_TH" -le 100 ] && CAUSAL_CONFIDENCE_THRESHOLD="$P_TH"
                        [ -n "$P_GW" ] && [[ "$P_GW" =~ ^[0-9]+$ ]] && [ "$P_GW" -gt 0 ] && GRAPH_COHORT_WINDOW="$P_GW"
                        [ -n "$P_GD" ] && awk -v v="$P_GD" 'BEGIN { exit !(v+0>=0) }' && GRAPH_TEMPORAL_DECAY_RATE="$P_GD"
                        CAUSAL_CONFIDENCE_SOURCE="policy"
                        GRAPH_TEMPORAL_SOURCE="policy"
                    elif [ "$REQUIRE_CAUSAL_GRAPHING_POLICY" = true ]; then
                        echo "Causal graphing policy failed: missing file $CAUSAL_GRAPHING_POLICY_FILE" >&2
                        exit 1
                    fi

                    SAMPLE_CONFIDENCE=0
                    if [ "$ATTESTATION_BASELINE_WINDOW" -gt 0 ]; then
                        SAMPLE_CONFIDENCE=$(( CAUSAL_CLUSTER_SAMPLE_COUNT * 100 / ATTESTATION_BASELINE_WINDOW ))
                    fi
                    [ "$SAMPLE_CONFIDENCE" -gt 100 ] && SAMPLE_CONFIDENCE=100

                    CONTEXT_SCORE=0
                    if [ "$SEASONAL_OVERLAY_SOURCE" = "policy" ]; then
                        CONTEXT_SCORE=100
                    elif [ "$SEASONAL_DECOMPOSITION_SOURCE" = "policy" ]; then
                        CONTEXT_SCORE=70
                    elif [ "$TIME_DECAY_SOURCE" = "policy" ]; then
                        CONTEXT_SCORE=40
                    fi

                    if [ "$ENABLE_GRAPH_EDGE_PERSISTENCE" = true ]; then
                        PERSIST_ROWS=$(awk -F',' -v tenant="$TENANT_ID" -v dep="$DEPENDENCY" -v phase="$ROLLOUT_PHASE" -v mode="$EFFECTIVE_FAILURE_MODE" 'NR>1 && $2==tenant && $4==dep && $3==phase && $5==mode { print $0 }' "$CAUSAL_HISTORY_FILE" | tail -n "$GRAPH_COHORT_WINDOW")
                        EDGE_PERSISTENCE_PERCENT=$(printf "%s\n" "$PERSIST_ROWS" | awk -F',' -v d="$GRAPH_TEMPORAL_DECAY_RATE" 'NF>0 { n++; age=n-1; w=exp(-1*d*age); tw+=w; if($6=="fail") fw+=w } END { if(tw>0) printf "%d", (fw*100)/tw; else print 0 }')
                        EDGE_PERSISTENCE_SCORE="$EDGE_PERSISTENCE_PERCENT"
                    else
                        GRAPH_TEMPORAL_SOURCE="disabled"
                    fi

                    CAUSAL_CONFIDENCE_SCORE=$(awk -v s="$SAMPLE_CONFIDENCE" -v f="$CAUSAL_CLUSTER_FAIL_PERCENT" -v c="$CONTEXT_SCORE" -v p="$EDGE_PERSISTENCE_SCORE" -v sw="$CONF_SAMPLE_WEIGHT" -v fw="$CONF_FAIL_WEIGHT" -v cw="$CONF_CONTEXT_WEIGHT" -v pw="$CONF_PERSIST_WEIGHT" 'BEGIN { v=(s*sw)+(f*fw)+(c*cw)+(p*pw); if (v>100) v=100; if (v<0) v=0; printf "%d", v }')
                    CAUSAL_GRAPH_NODE="$CAUSAL_CLUSTER_KEY|confidence=$CAUSAL_CONFIDENCE_SCORE"

                    if [ "$CAUSAL_CONFIDENCE_SCORE" -lt "$CAUSAL_CONFIDENCE_THRESHOLD" ]; then
                        CAUSAL_CLUSTER_SUGGESTED_OVERLAY="none"
                        CAUSAL_CLUSTER_SUGGESTED_MULTIPLIER=1.00
                    fi

                    CAUSAL_GRAPH_EDGE="$CAUSAL_CLUSTER_KEY -> ${CAUSAL_CLUSTER_SUGGESTED_OVERLAY:-none} (score=$CAUSAL_CONFIDENCE_SCORE,persistence=$EDGE_PERSISTENCE_SCORE)"
                fi
            fi
        fi

        CALIBRATED_ATTESTATION_THRESHOLD_PERCENT="$ATTESTATION_ANOMALY_THRESHOLD_PERCENT"
        TENANT_CRITICALITY_TIER="not-configured"
        ADAPTIVE_THRESHOLD_SOURCE="static"

        if [ "$ENABLE_ADAPTIVE_THRESHOLD_CALIBRATION" = true ]; then
            if [ ! -f "$TENANT_CRITICALITY_FILE" ] || [ ! -r "$TENANT_CRITICALITY_FILE" ]; then
                if [ "$REQUIRE_ADAPTIVE_THRESHOLD_POLICY" = true ]; then
                    echo "Adaptive threshold calibration failed: missing tenant criticality file $TENANT_CRITICALITY_FILE" >&2
                    exit 1
                fi
            elif [ ! -f "$ADAPTIVE_THRESHOLD_POLICY_FILE" ] || [ ! -r "$ADAPTIVE_THRESHOLD_POLICY_FILE" ]; then
                if [ "$REQUIRE_ADAPTIVE_THRESHOLD_POLICY" = true ]; then
                    echo "Adaptive threshold calibration failed: missing threshold policy file $ADAPTIVE_THRESHOLD_POLICY_FILE" >&2
                    exit 1
                fi
            else
                TENANT_CRITICALITY_TIER=$(awk -F',' -v tenant="$TENANT_ID" 'NR > 1 && $1 == tenant { print $2; exit }' "$TENANT_CRITICALITY_FILE" | tr -d '\r[:space:]')
                if [ -n "$TENANT_CRITICALITY_TIER" ]; then
                    TIER_THRESHOLD_VALUE=$(awk -F',' -v tier="$TENANT_CRITICALITY_TIER" 'NR > 1 && $1 == tier { print $2; exit }' "$ADAPTIVE_THRESHOLD_POLICY_FILE" | tr -d '\r[:space:]')
                    if [ -n "$TIER_THRESHOLD_VALUE" ] && [[ "$TIER_THRESHOLD_VALUE" =~ ^[0-9]+$ ]]; then
                        CALIBRATED_ATTESTATION_THRESHOLD_PERCENT="$TIER_THRESHOLD_VALUE"
                        ADAPTIVE_THRESHOLD_SOURCE="tier-policy"
                    elif [ "$REQUIRE_ADAPTIVE_THRESHOLD_POLICY" = true ]; then
                        echo "Adaptive threshold calibration failed: unresolved threshold for tier '$TENANT_CRITICALITY_TIER'" >&2
                        exit 1
                    fi
                elif [ "$REQUIRE_ADAPTIVE_THRESHOLD_POLICY" = true ]; then
                    echo "Adaptive threshold calibration failed: unresolved tenant criticality tier for '$TENANT_ID'" >&2
                    exit 1
                fi
            fi
        fi

        EFFECTIVE_ANOMALY_PERCENT="$ATTESTATION_ANOMALY_PERCENT"
        if [ "$ENABLE_TIME_DECAY_WEIGHTING" = true ]; then
            EFFECTIVE_ANOMALY_PERCENT="$WEIGHTED_ANOMALY_PERCENT"
        fi

        if [ "$REQUIRE_ATTESTATION_BASELINE_GATE" = true ] && [ "$EFFECTIVE_ANOMALY_PERCENT" -gt "$CALIBRATED_ATTESTATION_THRESHOLD_PERCENT" ]; then
            echo "Attestation anomaly baseline breach: $EFFECTIVE_ANOMALY_PERCENT% exceeds $CALIBRATED_ATTESTATION_THRESHOLD_PERCENT% for tenant $TENANT_ID" >&2
            exit 1
        fi

        ATTESTATION_TREND_RECORD="$TIMESTAMP,$TENANT_ID,$DISTRIBUTION_VALUE,$ATTESTATION_STATUS_VALUE,$SLO_STATUS_VALUE,$SLO_AGE_VALUE,$SIMULATION_ID"

        if [ "$DRY_RUN" = true ]; then
            echo "[DRY-RUN] Would append attestation trend record to $ATTESTATION_TREND_HISTORY_FILE:"
            echo "$ATTESTATION_TREND_RECORD"
        else
            printf "%s\n" "$ATTESTATION_TREND_RECORD" >> "$ATTESTATION_TREND_HISTORY_FILE"
        fi

        DASHBOARD_ENTRY=$(cat << EOF
## [ATTN-$ENTRY_ID] tenant_attestation_dashboard

**Logged**: $TIMESTAMP
**Tenant ID**: $TENANT_ID
**Distribution ID**: $DISTRIBUTION_VALUE
**Attestation Status**: $ATTESTATION_STATUS_VALUE
**Revocation SLO Status**: $SLO_STATUS_VALUE
**Revocation SLO Age Hours**: $SLO_AGE_VALUE
**Baseline Window**: $ATTESTATION_BASELINE_WINDOW
**Baseline Samples**: $ATTESTATION_BASELINE_TOTAL
**Baseline Alert Count**: $ATTESTATION_BASELINE_ALERT_COUNT
**Anomaly Percent**: $ATTESTATION_ANOMALY_PERCENT%
**Weighted Anomaly Percent**: $WEIGHTED_ANOMALY_PERCENT%
**Anomaly Threshold Percent**: $ATTESTATION_ANOMALY_THRESHOLD_PERCENT%
**Calibrated Threshold Percent**: $CALIBRATED_ATTESTATION_THRESHOLD_PERCENT%
**Tenant Criticality Tier**: $TENANT_CRITICALITY_TIER
**Adaptive Threshold Source**: $ADAPTIVE_THRESHOLD_SOURCE
**Time-Decay Half-Life Samples**: $DECAY_HALF_LIFE_EFFECTIVE
**Time-Decay Source**: $TIME_DECAY_SOURCE
**Time-Decay Weighting Enabled**: $ENABLE_TIME_DECAY_WEIGHTING
**Recurrence Ratio Percent**: $RECURRENCE_RATIO_PERCENT%
**Recurrence Tuned Half-Life Samples**: $RECURRENCE_TUNED_HALF_LIFE
**Recurrence Auto-Tuning Source**: $RECURRENCE_TUNING_SOURCE
**Recurrence Auto-Tuning Enabled**: $ENABLE_RECURRENCE_AUTO_TUNING
**Seasonal Phase**: $SEASONAL_PHASE
**Seasonal Multiplier**: $SEASONAL_MULTIPLIER
**Seasonal Tuned Half-Life Samples**: $SEASONAL_TUNED_HALF_LIFE
**Seasonal Decomposition Source**: $SEASONAL_DECOMPOSITION_SOURCE
**Seasonal Decomposition Enabled**: $ENABLE_SEASONAL_RECURRENCE_DECOMPOSITION
**Seasonal Overlay Name**: $SEASONAL_OVERLAY_NAME
**Seasonal Overlay Multiplier**: $SEASONAL_OVERLAY_MULTIPLIER
**Seasonal Overlay Source**: $SEASONAL_OVERLAY_SOURCE
**Seasonal Overlay Tuning Enabled**: $ENABLE_SEASONAL_OVERLAY_TUNING
**Causal Cluster Key**: $CAUSAL_CLUSTER_KEY
**Causal Cluster Sample Count**: $CAUSAL_CLUSTER_SAMPLE_COUNT
**Causal Cluster Fail Count**: $CAUSAL_CLUSTER_FAIL_COUNT
**Causal Cluster Fail Percent**: $CAUSAL_CLUSTER_FAIL_PERCENT%
**Suggested Overlay Candidate**: $CAUSAL_CLUSTER_SUGGESTED_OVERLAY
**Suggested Overlay Multiplier**: $CAUSAL_CLUSTER_SUGGESTED_MULTIPLIER
**Causal Clustering Source**: $CAUSAL_CLUSTER_SOURCE
**Causal Clustering Enabled**: $ENABLE_CAUSAL_CLUSTERING
**Causal Confidence Score**: $CAUSAL_CONFIDENCE_SCORE
**Causal Confidence Threshold**: $CAUSAL_CONFIDENCE_THRESHOLD
**Causal Confidence Source**: $CAUSAL_CONFIDENCE_SOURCE
**Graph Cohort Window**: $GRAPH_COHORT_WINDOW
**Graph Temporal Decay Rate**: $GRAPH_TEMPORAL_DECAY_RATE
**Graph Temporal Source**: ${GRAPH_TEMPORAL_SOURCE:-disabled}
**Edge Persistence Percent**: ${EDGE_PERSISTENCE_PERCENT:-0}%
**Edge Persistence Score**: ${EDGE_PERSISTENCE_SCORE:-0}
**Graph Edge Persistence Enabled**: $ENABLE_GRAPH_EDGE_PERSISTENCE
**Causal Graph Node**: $CAUSAL_GRAPH_NODE
**Causal Graph Edge**: $CAUSAL_GRAPH_EDGE
**Anomaly Gate Enabled**: $REQUIRE_ATTESTATION_BASELINE_GATE

---
EOF
)
CAUSAL_GRAPH_ENTRY=$(cat << EOF
## [GRAPH-$ENTRY_ID] confidence_weighted_causal_graph

**Logged**: $TIMESTAMP
**Tenant ID**: $TENANT_ID
**Node**: ${CAUSAL_GRAPH_NODE:-none}
**Edge**: ${CAUSAL_GRAPH_EDGE:-none}
**Confidence Score**: ${CAUSAL_CONFIDENCE_SCORE:-0}
**Confidence Threshold**: ${CAUSAL_CONFIDENCE_THRESHOLD:-60}
**Confidence Source**: ${CAUSAL_CONFIDENCE_SOURCE:-disabled}

---
EOF
)

        if [ "$DRY_RUN" = true ]; then
            echo "[DRY-RUN] Would append tenant attestation dashboard entry to $ATTESTATION_DASHBOARD_FILE:"
            echo "$DASHBOARD_ENTRY"
        else
            if [ ! -f "$ATTESTATION_DASHBOARD_FILE" ]; then
                printf "%s\n\n" "# Tenant Trust-Policy Attestation Dashboard" > "$ATTESTATION_DASHBOARD_FILE"
            fi
            printf "%s\n" "$DASHBOARD_ENTRY" >> "$ATTESTATION_DASHBOARD_FILE"
        fi

        if [ "$ENABLE_CROSS_TENANT_HEATMAP" = true ]; then
            CROSS_TENANT_HISTORY=$(awk -F',' 'NR > 1 { print $0 }' "$ATTESTATION_TREND_HISTORY_FILE" | tail -n "$ATTESTATION_BASELINE_WINDOW")
            UNIQUE_TENANTS=$(printf "%s\n" "$CROSS_TENANT_HISTORY" | awk -F',' 'NF > 0 { print $2 }' | sed '/^$/d' | sort -u)

            if [ -z "$UNIQUE_TENANTS" ]; then
                CROSS_TENANT_HEATMAP_LINES="- tenant: none, anomaly-percent: 0, severity: low"
            else
                CROSS_TENANT_HEATMAP_LINES=""
                while IFS= read -r tenant; do
                    [ -z "$tenant" ] && continue
                    TENANT_ROWS=$(printf "%s\n" "$CROSS_TENANT_HISTORY" | awk -F',' -v t="$tenant" 'NF > 0 && $2 == t { print $0 }')
                    TENANT_SAMPLES=$(printf "%s\n" "$TENANT_ROWS" | sed '/^$/d' | wc -l | tr -d ' ')
                    TENANT_ALERTS=$(printf "%s\n" "$TENANT_ROWS" | awk -F',' 'NF > 0 && ($4 != "verified" || $5 == "warning") { c++ } END { print c + 0 }')
                    TENANT_PERCENT=0
                    if [ "$TENANT_SAMPLES" -gt 0 ]; then
                        TENANT_PERCENT=$(( TENANT_ALERTS * 100 / TENANT_SAMPLES ))
                    fi

                    TENANT_SEVERITY="low"
                    TENANT_ROUTE="monitor"
                    if [ "$TENANT_PERCENT" -ge 80 ]; then
                        TENANT_SEVERITY="critical"
                        TENANT_ROUTE="auto-remediation"
                    elif [ "$TENANT_PERCENT" -ge 60 ]; then
                        TENANT_SEVERITY="high"
                        TENANT_ROUTE="incident-review"
                    elif [ "$TENANT_PERCENT" -ge 35 ]; then
                        TENANT_SEVERITY="medium"
                        TENANT_ROUTE="owner-review"
                    fi

                    CROSS_TENANT_HEATMAP_LINES+="- tenant: $tenant, anomaly-percent: $TENANT_PERCENT, severity: $TENANT_SEVERITY, route: $TENANT_ROUTE"$'\n'
                done <<< "$UNIQUE_TENANTS"
            fi

            HEATMAP_ENTRY=$(cat << EOF
## [HEATMAP-$ENTRY_ID] cross_tenant_attestation_anomaly

**Logged**: $TIMESTAMP
**Baseline Window**: $ATTESTATION_BASELINE_WINDOW
**Anomaly Threshold Percent**: $ATTESTATION_ANOMALY_THRESHOLD_PERCENT

### Tenant Heatmap
$CROSS_TENANT_HEATMAP_LINES

---
EOF
)

            ROUTING_ENTRY=$(cat << EOF
## [ROUTE-$ENTRY_ID] cross_tenant_auto_remediation_routing

**Logged**: $TIMESTAMP
**Source Simulation**: $SIMULATION_ID
**Tenant ID**: $TENANT_ID
**Anomaly Percent**: ${ATTESTATION_ANOMALY_PERCENT:-0}%
**Routing Recommendation**: $(if [ "${ATTESTATION_ANOMALY_PERCENT:-0}" -ge 80 ]; then echo "auto-remediation"; elif [ "${ATTESTATION_ANOMALY_PERCENT:-0}" -ge 60 ]; then echo "incident-review"; elif [ "${ATTESTATION_ANOMALY_PERCENT:-0}" -ge 35 ]; then echo "owner-review"; else echo "monitor"; fi)
**Target Endpoint**: gateway.runtime.governance.remediationPlan
**Routing Policy**: tenant anomaly severity bands

---
EOF
)

            if [ "$DRY_RUN" = true ]; then
                echo "[DRY-RUN] Would append cross-tenant heatmap entry to $CROSS_TENANT_HEATMAP_FILE:"
                echo "$HEATMAP_ENTRY"
                echo "[DRY-RUN] Would append auto-remediation routing entry to $AUTO_REMEDIATION_ROUTING_FILE:"
                echo "$ROUTING_ENTRY"
            else
                if [ ! -f "$CROSS_TENANT_HEATMAP_FILE" ]; then
                    printf "%s\n\n" "# Cross-Tenant Attestation Anomaly Heatmap" > "$CROSS_TENANT_HEATMAP_FILE"
                fi
                if [ ! -f "$AUTO_REMEDIATION_ROUTING_FILE" ]; then
                    printf "%s\n\n" "# Cross-Tenant Auto-Remediation Recommendation Routing" > "$AUTO_REMEDIATION_ROUTING_FILE"
                fi
                printf "%s\n" "$HEATMAP_ENTRY" >> "$CROSS_TENANT_HEATMAP_FILE"
                printf "%s\n" "$ROUTING_ENTRY" >> "$AUTO_REMEDIATION_ROUTING_FILE"
            fi
        fi
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
- Manifest Signature Scheme: ${MANIFEST_SIGNATURE_SCHEME:-not-verified}
- Manifest Signature File: ${MANIFEST_SIGNATURE_FILE:-not-verified}
- Manifest Certificate File: ${MANIFEST_CERTIFICATE_FILE:-not-verified}
- Weight Schema Version: ${_schema:-not-declared}
- Strict Schema Gate: ${STRICT_SCHEMA_VERSION:-disabled}
- Strict Manifest Gate: ${REQUIRE_SIGNED_MANIFEST}
- Signature Verification Mode: ${SIGNATURE_VERIFICATION_MODE}
- Trust Policy Source: ${TRUST_POLICY_FILE}
- Trust Policy Distribution Id: ${TRUST_POLICY_DISTRIBUTION_ID:-not-verified}
- Trust Policy Distributed At: ${TRUST_POLICY_DISTRIBUTED_AT:-not-verified}
- Trust Policy Federation Scope: ${TRUST_POLICY_FEDERATION_SCOPE:-not-verified}
- Trust Policy Active Key Count: ${TRUST_POLICY_ACTIVE_KEY_COUNT:-0}
- Trust Policy Revoked Key Count: ${TRUST_POLICY_REVOKED_KEY_COUNT:-0}
- Trust Policy Gate: ${REQUIRE_TRUST_POLICY}
- Trust Policy Attestation Source: ${TRUST_POLICY_ATTESTATION_FILE}
- Trust Policy Attestation Distribution Id: ${ATTESTATION_DISTRIBUTION_ID:-not-verified}
- Trust Policy Attestation Published At: ${ATTESTATION_PUBLISHED_AT:-not-verified}
- Trust Policy Attestation Status: ${ATTESTATION_STATUS:-not-verified}
- Trust Policy Attestation Signer: ${ATTESTATION_SIGNER:-not-verified}
- Trust Policy Attestation Gate: ${REQUIRE_TRUST_POLICY_ATTESTATION}
- Revocation List Source: ${REVOCATION_LIST_FILE}
- Revocation Check Gate: ${REQUIRE_REVOCATION_CHECK}
- Revocation SLO Source: ${REVOCATION_SLO_FILE}
- Revocation SLO Max Hours: ${REVOCATION_SLO_MAX_HOURS:-not-verified}
- Revocation SLO Last Check At: ${REVOCATION_SLO_LAST_CHECK_AT:-not-verified}
- Revocation SLO Status: ${REVOCATION_SLO_STATUS:-not-verified}
- Revocation SLO Gate: ${REQUIRE_REVOCATION_SLO}
- Attestation Dashboard Source: ${ATTESTATION_DASHBOARD_FILE}
- Attestation Trend History Source: ${ATTESTATION_TREND_HISTORY_FILE}
- Attestation Baseline Window: ${ATTESTATION_BASELINE_WINDOW}
- Attestation Anomaly Threshold Percent: ${ATTESTATION_ANOMALY_THRESHOLD_PERCENT}
- Attestation Baseline Samples: ${ATTESTATION_BASELINE_TOTAL:-0}
- Attestation Baseline Alert Count: ${ATTESTATION_BASELINE_ALERT_COUNT:-0}
- Attestation Anomaly Percent: ${ATTESTATION_ANOMALY_PERCENT:-0}%
- Attestation Weighted Anomaly Percent: ${WEIGHTED_ANOMALY_PERCENT:-$ATTESTATION_ANOMALY_PERCENT}%
- Attestation Calibrated Threshold Percent: ${CALIBRATED_ATTESTATION_THRESHOLD_PERCENT:-$ATTESTATION_ANOMALY_THRESHOLD_PERCENT}
- Tenant Criticality Tier: ${TENANT_CRITICALITY_TIER:-not-configured}
- Adaptive Threshold Source: ${ADAPTIVE_THRESHOLD_SOURCE:-static}
- Adaptive Threshold Calibration Enabled: ${ENABLE_ADAPTIVE_THRESHOLD_CALIBRATION}
- Time-Decay Half-Life Samples: ${DECAY_HALF_LIFE_EFFECTIVE:-$TIME_DECAY_HALF_LIFE}
- Time-Decay Source: ${TIME_DECAY_SOURCE:-disabled}
- Time-Decay Weighting Enabled: ${ENABLE_TIME_DECAY_WEIGHTING}
- Recurrence Ratio Percent: ${RECURRENCE_RATIO_PERCENT:-0}%
- Recurrence Tuned Half-Life Samples: ${RECURRENCE_TUNED_HALF_LIFE:-$TIME_DECAY_HALF_LIFE}
- Recurrence Auto-Tuning Source: ${RECURRENCE_TUNING_SOURCE:-disabled}
- Recurrence Auto-Tuning Enabled: ${ENABLE_RECURRENCE_AUTO_TUNING}
- Seasonal Phase: ${SEASONAL_PHASE:-not-applied}
- Seasonal Multiplier: ${SEASONAL_MULTIPLIER:-1.0}
- Seasonal Tuned Half-Life Samples: ${SEASONAL_TUNED_HALF_LIFE:-$TIME_DECAY_HALF_LIFE}
- Seasonal Decomposition Source: ${SEASONAL_DECOMPOSITION_SOURCE:-disabled}
- Seasonal Decomposition Enabled: ${ENABLE_SEASONAL_RECURRENCE_DECOMPOSITION}
- Seasonal Overlay Name: ${SEASONAL_OVERLAY_NAME:-none}
- Seasonal Overlay Multiplier: ${SEASONAL_OVERLAY_MULTIPLIER:-1.0}
- Seasonal Overlay Source: ${SEASONAL_OVERLAY_SOURCE:-disabled}
- Seasonal Overlay Tuning Enabled: ${ENABLE_SEASONAL_OVERLAY_TUNING}
- Causal Cluster Key: ${CAUSAL_CLUSTER_KEY:-none}
- Causal Cluster Sample Count: ${CAUSAL_CLUSTER_SAMPLE_COUNT:-0}
- Causal Cluster Fail Count: ${CAUSAL_CLUSTER_FAIL_COUNT:-0}
- Causal Cluster Fail Percent: ${CAUSAL_CLUSTER_FAIL_PERCENT:-0}%
- Suggested Overlay Candidate: ${CAUSAL_CLUSTER_SUGGESTED_OVERLAY:-none}
- Suggested Overlay Multiplier: ${CAUSAL_CLUSTER_SUGGESTED_MULTIPLIER:-1.00}
- Causal Clustering Source: ${CAUSAL_CLUSTER_SOURCE:-disabled}
- Causal Clustering Enabled: ${ENABLE_CAUSAL_CLUSTERING}
- Causal Confidence Score: ${CAUSAL_CONFIDENCE_SCORE:-0}
- Causal Confidence Threshold: ${CAUSAL_CONFIDENCE_THRESHOLD:-60}
- Causal Confidence Source: ${CAUSAL_CONFIDENCE_SOURCE:-disabled}
- Graph Cohort Window: ${GRAPH_COHORT_WINDOW}
- Graph Temporal Decay Rate: ${GRAPH_TEMPORAL_DECAY_RATE}
- Graph Temporal Source: ${GRAPH_TEMPORAL_SOURCE:-disabled}
- Edge Persistence Percent: ${EDGE_PERSISTENCE_PERCENT:-0}%
- Edge Persistence Score: ${EDGE_PERSISTENCE_SCORE:-0}
- Graph Edge Persistence Enabled: ${ENABLE_GRAPH_EDGE_PERSISTENCE}
- Causal Graph Node: ${CAUSAL_GRAPH_NODE:-none}
- Causal Graph Edge: ${CAUSAL_GRAPH_EDGE:-none}
- Attestation Baseline Gate: ${REQUIRE_ATTESTATION_BASELINE_GATE}
- Cross-Tenant Heatmap Source: ${CROSS_TENANT_HEATMAP_FILE}
- Auto-Remediation Routing Source: ${AUTO_REMEDIATION_ROUTING_FILE}
- Cross-Tenant Heatmap Enabled: ${ENABLE_CROSS_TENANT_HEATMAP}
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
CAUSAL_HISTORY_RECORD="$TIMESTAMP,$TENANT_ID,$ROLLOUT_PHASE,$DEPENDENCY,${FAILURE_MODE:-unspecified},$RESULT,$SIMULATION_ID"
OVERLAY_CANDIDATE_ENTRY=$(cat << EOF
## [OVR-$ENTRY_ID] overlay_candidate

**Logged**: $TIMESTAMP
**Tenant ID**: $TENANT_ID
**Cluster Key**: ${CAUSAL_CLUSTER_KEY:-none}
**Sample Count**: ${CAUSAL_CLUSTER_SAMPLE_COUNT:-0}
**Fail Count**: ${CAUSAL_CLUSTER_FAIL_COUNT:-0}
**Fail Percent**: ${CAUSAL_CLUSTER_FAIL_PERCENT:-0}%
**Suggested Overlay Name**: ${CAUSAL_CLUSTER_SUGGESTED_OVERLAY:-none}
**Suggested Overlay Multiplier**: ${CAUSAL_CLUSTER_SUGGESTED_MULTIPLIER:-1.00}
**Source**: ${CAUSAL_CLUSTER_SOURCE:-disabled}

---
EOF
)

if [ "$DRY_RUN" = true ]; then
    echo "[DRY-RUN] Would append to $LEARNINGS_FILE:"
    echo "$LEARNING_ENTRY"
    echo "[DRY-RUN] Would append to $POLICY_TUNING_FILE:"
    echo "$POLICY_ENTRY"
    echo "[DRY-RUN] Would append trend record to $TREND_HISTORY_FILE:"
    echo "$TREND_RECORD"
    echo "[DRY-RUN] Would append causal history record to $CAUSAL_HISTORY_FILE:"
    echo "$CAUSAL_HISTORY_RECORD"
    echo "[DRY-RUN] Would append overlay candidate suggestion to $OVERLAY_CANDIDATE_FILE:"
    echo "$OVERLAY_CANDIDATE_ENTRY"
    echo "[DRY-RUN] Would append causal confidence graph entry to $CAUSAL_GRAPH_FILE:"
    echo "$CAUSAL_GRAPH_ENTRY"
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
printf "%s\n" "$CAUSAL_HISTORY_RECORD" >> "$CAUSAL_HISTORY_FILE"
if [ ! -f "$OVERLAY_CANDIDATE_FILE" ]; then
    printf "%s\n\n" "# Seasonal Overlay Candidate Suggestions" > "$OVERLAY_CANDIDATE_FILE"
fi
printf "%s\n" "$OVERLAY_CANDIDATE_ENTRY" >> "$OVERLAY_CANDIDATE_FILE"
if [ ! -f "$CAUSAL_GRAPH_FILE" ]; then
    printf "%s\n\n" "# Confidence-Weighted Causal Graph" > "$CAUSAL_GRAPH_FILE"
fi
printf "%s\n" "$CAUSAL_GRAPH_ENTRY" >> "$CAUSAL_GRAPH_FILE"

echo "Appended outage simulation learning entry to $LEARNINGS_FILE"
echo "Appended policy tuning recommendation to $POLICY_TUNING_FILE"
echo "Appended tenant trend record to $TREND_HISTORY_FILE"
if [ "$ENABLE_ATTESTATION_DASHBOARD" = true ]; then
    echo "Updated tenant attestation trend history at $ATTESTATION_TREND_HISTORY_FILE"
    echo "Updated tenant attestation dashboard at $ATTESTATION_DASHBOARD_FILE"
    if [ "$ENABLE_CROSS_TENANT_HEATMAP" = true ]; then
        echo "Updated cross-tenant anomaly heatmap at $CROSS_TENANT_HEATMAP_FILE"
        echo "Updated auto-remediation routing at $AUTO_REMEDIATION_ROUTING_FILE"
    fi
fi
