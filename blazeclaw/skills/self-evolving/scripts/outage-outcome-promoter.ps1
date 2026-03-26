#!/usr/bin/env pwsh
# Outage Outcome Promoter (BlazeClaw) - PowerShell
# Captures outage simulation outcomes and emits policy tuning recommendations.
# Adds tenant-scoped trend analysis and phase-aware recommendation scoring.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$learningsFile = './blazeclaw/skills/self-evolving/.learnings/LEARNINGS.md'
$policyTuningFile =
    './blazeclaw/skills/self-evolving/.learnings/POLICY_TUNING_RECOMMENDATIONS.md'
$trendHistoryFile =
    './blazeclaw/skills/self-evolving/.learnings/OUTAGE_TREND_HISTORY.csv'
$profileWeightsFile =
    './blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.csv'
$profileManifestFile =
    './blazeclaw/skills/self-evolving/assets/policy-profile-scoring-weights.manifest'
$trustPolicyFile =
    './blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.conf'
$revocationListFile =
    './blazeclaw/skills/self-evolving/assets/policy-profile-key-revocations.csv'
$trustPolicyAttestationFile =
    './blazeclaw/skills/self-evolving/assets/policy-profile-trust-policy.attestation'
$revocationSloFile =
    './blazeclaw/skills/self-evolving/assets/policy-profile-revocation-slo.conf'
$attestationDashboardFile =
    './blazeclaw/skills/self-evolving/.learnings/TENANT_TRUST_POLICY_ATTESTATION_DASHBOARD.md'
$attestationTrendHistoryFile =
    './blazeclaw/skills/self-evolving/.learnings/TENANT_TRUST_POLICY_ATTESTATION_HISTORY.csv'
$crossTenantHeatmapFile =
    './blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_ATTESTATION_ANOMALY_HEATMAP.md'
$autoRemediationRoutingFile =
    './blazeclaw/skills/self-evolving/.learnings/CROSS_TENANT_AUTO_REMEDIATION_ROUTING.md'
$tenantCriticalityFile =
    './blazeclaw/skills/self-evolving/assets/tenant-criticality-tiers.csv'
$adaptiveThresholdPolicyFile =
    './blazeclaw/skills/self-evolving/assets/attestation-anomaly-threshold-tiers.csv'
$timeDecayPolicyFile =
    './blazeclaw/skills/self-evolving/assets/attestation-anomaly-time-decay-policy.conf'
$recurrenceTuningPolicyFile =
    './blazeclaw/skills/self-evolving/assets/attestation-anomaly-recurrence-tuning-policy.conf'
$seasonalDecompositionPolicyFile =
    './blazeclaw/skills/self-evolving/assets/attestation-anomaly-seasonal-decomposition-policy.conf'
$seasonalOverlayPolicyFile =
    './blazeclaw/skills/self-evolving/assets/attestation-anomaly-seasonal-overlay-policy.csv'
$causalClusteringPolicyFile =
    './blazeclaw/skills/self-evolving/assets/attestation-anomaly-causal-clustering-policy.conf'
$causalHistoryFile =
    './blazeclaw/skills/self-evolving/.learnings/ANOMALY_CAUSAL_HISTORY.csv'
$overlayCandidateFile =
    './blazeclaw/skills/self-evolving/.learnings/SEASONAL_OVERLAY_CANDIDATES.md'
$causalGraphingPolicyFile =
    './blazeclaw/skills/self-evolving/assets/attestation-anomaly-causal-graphing-policy.conf'
$causalGraphFile =
    './blazeclaw/skills/self-evolving/.learnings/CAUSAL_CONFIDENCE_GRAPH.md'
$graphExplainabilityFile =
    './blazeclaw/skills/self-evolving/.learnings/CAUSAL_GRAPH_EXPLAINABILITY_TRACES.md'
$explainabilityHistoryFile =
    './blazeclaw/skills/self-evolving/.learnings/CAUSAL_GRAPH_EXPLAINABILITY_HISTORY.csv'
$driftRootCauseFile =
    './blazeclaw/skills/self-evolving/.learnings/CAUSAL_DRIFT_ROOT_CAUSE_NARRATIVES.md'

function Show-Usage {
@"
Usage: $(Split-Path -Leaf $PSCommandPath) --simulation-id <id> --tenant-id <tenant> --rollout-phase <r1|r2|r3|r4> --dependency <registry|authority> --result <pass|fail> --evidence-path <path> [options]

Required:
  --simulation-id       Outage simulation identifier (example: SIM-REG-001)
  --tenant-id           Tenant identifier for trend analysis
  --rollout-phase       Rollout phase (r1|r2|r3|r4)
  --policy-profile      Policy profile for configurable score weights
  --dependency          Target dependency (registry|authority)
  --result              Simulation result (pass|fail)
  --evidence-path       Path to drill evidence artifact

Optional:
  --drill-window        Drill execution window
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
  --require-trust-policy-attestation Enforce trust-policy attestation checks
  --revocation-slo-file Revocation propagation SLO policy file
  --require-revocation-slo Enforce revocation SLO checks
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
  --graph-explainability-file Cohort-aware explainability trace markdown output
  --disable-graph-explainability-traces Disable explainability trace output
  --disable-explainer-diffing Disable consecutive cohort explainer diffing
  --explainer-drift-threshold Drift threshold for audit diff alerts (default: 15)
  --drift-root-cause-file Root-cause narrative markdown output for flagged drift
  --disable-drift-root-cause-synthesis Disable root-cause narrative synthesis for flagged drift
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
"@
}

$simulationId = ''
$tenantId = ''
$rolloutPhase = ''
$policyProfile = 'default'
$strictSchemaVersion = ''
$requireSignedManifest = $false
$requireTrustPolicy = $false
$requireRevocationCheck = $false
$requireTrustPolicyAttestation = $false
$requireRevocationSlo = $false
$enableAttestationDashboard = $true
$enableCrossTenantHeatmap = $true
$attestationBaselineWindow = 20
$attestationAnomalyThresholdPercent = 25
$requireAttestationBaselineGate = $false
$enableAdaptiveThresholdCalibration = $true
$requireAdaptiveThresholdPolicy = $false
$enableTimeDecayWeighting = $true
$timeDecayHalfLife = 5
$requireTimeDecayPolicy = $false
$enableRecurrenceAutoTuning = $true
$requireRecurrenceTuningPolicy = $false
$enableSeasonalRecurrenceDecomposition = $true
$reportingCycleLength = 12
$requireSeasonalDecompositionPolicy = $false
$enableSeasonalOverlayTuning = $true
$requireSeasonalOverlayPolicy = $false
$enableCausalClustering = $true
$requireCausalClusteringPolicy = $false
$enableConfidenceCausalGraphing = $true
$requireCausalGraphingPolicy = $false
$enableGraphEdgePersistence = $true
$graphCohortWindow = 30
$graphTemporalDecayRate = 0.15
$enableGraphExplainabilityTraces = $true
$enableExplainerDiffing = $true
$explainerDriftThreshold = 15
$enableDriftRootCauseSynthesis = $true
$enableProbabilisticConfidenceBounds = $true
$confidenceBoundZScore = 1.96
$enableBayesianPosteriorConfidence = $true
$bayesPriorAlpha = 1.0
$bayesPriorBeta = 1.0
$manifestFileExplicit = $false
$signatureVerificationMode = 'none'
$kmsPublicKeyFile = ''
$sigstoreCertificateFile = ''
$sigstoreCertificateIdentity = ''
$sigstoreOidcIssuer = ''
$cosignPath = 'cosign'
$dependency = ''
$result = ''
$evidencePath = ''
$drillWindow = ''
$failureMode = ''
$failoverTriggered = ''
$failbackCompleted = ''
$manifestVersion = ''
$manifestSignedBy = ''
$manifestSignedAt = ''
$manifestKeyId = ''
$manifestSignature = ''
$manifestSignatureScheme = ''
$manifestSignatureFile = ''
$manifestCertificateFile = ''
$trustPolicyDistributionId = ''
$trustPolicyDistributedAt = ''
$trustPolicyFederationScope = ''
$trustPolicyActiveKeyCount = 0
$trustPolicyRevokedKeyCount = 0
$attestationDistributionId = ''
$attestationPublishedAt = ''
$attestationStatus = ''
$attestationSigner = ''
$revocationSloMaxHours = ''
$revocationSloLastCheckAt = ''
$revocationSloStatus = ''
$attestationBaselineSamples = 0
$attestationBaselineAlertCount = 0
$attestationAnomalyPercent = 0
$calibratedAttestationThresholdPercent = 25
$tenantCriticalityTier = 'not-configured'
$adaptiveThresholdSource = 'static'
$weightedAnomalyPercent = 0
$timeDecayHalfLifeEffective = 5
$timeDecaySource = 'disabled'
$recurrenceRatioPercent = 0
$recurrenceTunedHalfLife = 5
$recurrenceTuningSource = 'disabled'
$seasonalPhase = 'not-applied'
$seasonalMultiplier = 1.0
$seasonalTunedHalfLife = 5
$seasonalDecompositionSource = 'disabled'
$seasonalOverlayName = 'none'
$seasonalOverlayMultiplier = 1.0
$seasonalOverlaySource = 'disabled'
$causalClusterKey = 'none'
$causalClusterSampleCount = 0
$causalClusterFailCount = 0
$causalClusterFailPercent = 0
$causalClusterSuggestedOverlay = 'none'
$causalClusterSuggestedMultiplier = 1.00
$causalClusterSource = 'disabled'
$causalConfidenceScore = 0
$causalConfidenceThreshold = 60
$causalConfidenceSource = 'disabled'
$causalGraphNode = 'none'
$causalGraphEdge = 'none'
        $edgePersistenceScore = 0
        $edgePersistencePercent = 0
        $graphTemporalSource = 'disabled'
[int]$sampleConfidence = 0
[int]$contextScore = 0
[int]$explainSampleContrib = 0
[int]$explainFailContrib = 0
[int]$explainContextContrib = 0
[int]$explainPersistContrib = 0
$edgePersistenceScore = 0
$edgePersistencePercent = 0
$graphTemporalSource = 'disabled'
$notes = ''
$trendWindowSize = 20
$dryRun = $false

for ($i = 0; $i -lt $args.Length; $i++) {
    $arg = [string]$args[$i]
    switch ($arg) {
        '--simulation-id' {
            $i++
            $simulationId = [string]$args[$i]
        }
        '--tenant-id' {
            $i++
            $tenantId = [string]$args[$i]
        }
        '--rollout-phase' {
            $i++
            $rolloutPhase = [string]$args[$i]
        }
        '--policy-profile' {
            $i++
            $policyProfile = [string]$args[$i]
        }
        '--dependency' {
            $i++
            $dependency = [string]$args[$i]
        }
        '--result' {
            $i++
            $result = [string]$args[$i]
        }
        '--evidence-path' {
            $i++
            $evidencePath = [string]$args[$i]
        }
        '--drill-window' {
            $i++
            $drillWindow = [string]$args[$i]
        }
        '--failure-mode' {
            $i++
            $failureMode = [string]$args[$i]
        }
        '--failover-triggered' {
            $i++
            $failoverTriggered = [string]$args[$i]
        }
        '--failback-completed' {
            $i++
            $failbackCompleted = [string]$args[$i]
        }
        '--weights-file' {
            $i++
            $profileWeightsFile = [string]$args[$i]
        }
        '--manifest-file' {
            $i++
            $profileManifestFile = [string]$args[$i]
            $manifestFileExplicit = $true
        }
        '--require-signed-manifest' {
            $requireSignedManifest = $true
        }
        '--trust-policy-file' {
            $i++
            $trustPolicyFile = [string]$args[$i]
        }
        '--require-trust-policy' {
            $requireTrustPolicy = $true
        }
        '--revocation-file' {
            $i++
            $revocationListFile = [string]$args[$i]
        }
        '--require-revocation-check' {
            $requireRevocationCheck = $true
        }
        '--trust-policy-attestation-file' {
            $i++
            $trustPolicyAttestationFile = [string]$args[$i]
        }
        '--require-trust-policy-attestation' {
            $requireTrustPolicyAttestation = $true
        }
        '--revocation-slo-file' {
            $i++
            $revocationSloFile = [string]$args[$i]
        }
        '--require-revocation-slo' {
            $requireRevocationSlo = $true
        }
        '--attestation-dashboard-file' {
            $i++
            $attestationDashboardFile = [string]$args[$i]
        }
        '--attestation-history-file' {
            $i++
            $attestationTrendHistoryFile = [string]$args[$i]
        }
        '--attestation-baseline-window' {
            $i++
            $attestationBaselineWindow = [int]$args[$i]
        }
        '--attestation-anomaly-threshold-percent' {
            $i++
            $attestationAnomalyThresholdPercent = [int]$args[$i]
        }
        '--require-attestation-baseline-gate' {
            $requireAttestationBaselineGate = $true
        }
        '--disable-attestation-dashboard' {
            $enableAttestationDashboard = $false
        }
        '--cross-tenant-heatmap-file' {
            $i++
            $crossTenantHeatmapFile = [string]$args[$i]
        }
        '--auto-remediation-routing-file' {
            $i++
            $autoRemediationRoutingFile = [string]$args[$i]
        }
        '--disable-cross-tenant-heatmap' {
            $enableCrossTenantHeatmap = $false
        }
        '--tenant-criticality-file' {
            $i++
            $tenantCriticalityFile = [string]$args[$i]
        }
        '--adaptive-threshold-policy-file' {
            $i++
            $adaptiveThresholdPolicyFile = [string]$args[$i]
        }
        '--disable-adaptive-threshold-calibration' {
            $enableAdaptiveThresholdCalibration = $false
        }
        '--require-adaptive-threshold-policy' {
            $requireAdaptiveThresholdPolicy = $true
        }
        '--time-decay-policy-file' {
            $i++
            $timeDecayPolicyFile = [string]$args[$i]
        }
        '--time-decay-half-life' {
            $i++
            $timeDecayHalfLife = [int]$args[$i]
        }
        '--disable-time-decay-weighting' {
            $enableTimeDecayWeighting = $false
        }
        '--require-time-decay-policy' {
            $requireTimeDecayPolicy = $true
        }
        '--recurrence-tuning-policy-file' {
            $i++
            $recurrenceTuningPolicyFile = [string]$args[$i]
        }
        '--disable-recurrence-auto-tuning' {
            $enableRecurrenceAutoTuning = $false
        }
        '--require-recurrence-tuning-policy' {
            $requireRecurrenceTuningPolicy = $true
        }
        '--seasonal-decomposition-policy-file' {
            $i++
            $seasonalDecompositionPolicyFile = [string]$args[$i]
        }
        '--reporting-cycle-length' {
            $i++
            $reportingCycleLength = [int]$args[$i]
        }
        '--disable-seasonal-recurrence-decomposition' {
            $enableSeasonalRecurrenceDecomposition = $false
        }
        '--require-seasonal-decomposition-policy' {
            $requireSeasonalDecompositionPolicy = $true
        }
        '--seasonal-overlay-policy-file' {
            $i++
            $seasonalOverlayPolicyFile = [string]$args[$i]
        }
        '--disable-seasonal-overlay-tuning' {
            $enableSeasonalOverlayTuning = $false
        }
        '--require-seasonal-overlay-policy' {
            $requireSeasonalOverlayPolicy = $true
        }
        '--causal-clustering-policy-file' {
            $i++
            $causalClusteringPolicyFile = [string]$args[$i]
        }
        '--overlay-candidate-file' {
            $i++
            $overlayCandidateFile = [string]$args[$i]
        }
        '--disable-causal-clustering' {
            $enableCausalClustering = $false
        }
        '--require-causal-clustering-policy' {
            $requireCausalClusteringPolicy = $true
        }
        '--causal-graphing-policy-file' {
            $i++
            $causalGraphingPolicyFile = [string]$args[$i]
        }
        '--causal-graph-file' {
            $i++
            $causalGraphFile = [string]$args[$i]
        }
        '--disable-confidence-causal-graphing' {
            $enableConfidenceCausalGraphing = $false
        }
        '--require-causal-graphing-policy' {
            $requireCausalGraphingPolicy = $true
        }
        '--graph-cohort-window' {
            $i++
            $graphCohortWindow = [int]$args[$i]
        }
        '--graph-temporal-decay-rate' {
            $i++
            $graphTemporalDecayRate = [double]$args[$i]
        }
        '--disable-graph-edge-persistence' {
            $enableGraphEdgePersistence = $false
        }
        '--graph-explainability-file' {
            $i++
            $graphExplainabilityFile = [string]$args[$i]
        }
        '--disable-graph-explainability-traces' {
            $enableGraphExplainabilityTraces = $false
        }
        '--disable-explainer-diffing' {
            $enableExplainerDiffing = $false
        }
        '--explainer-drift-threshold' {
            $i++
            $explainerDriftThreshold = [int]$args[$i]
        }
        '--drift-root-cause-file' {
            $i++
            $driftRootCauseFile = [string]$args[$i]
        }
        '--disable-drift-root-cause-synthesis' {
            $enableDriftRootCauseSynthesis = $false
        }
        '--disable-probabilistic-confidence-bounds' {
            $enableProbabilisticConfidenceBounds = $false
        }
        '--confidence-bound-zscore' {
            $i++
            $confidenceBoundZScore = [double]$args[$i]
        }
        '--disable-bayesian-posterior-confidence' {
            $enableBayesianPosteriorConfidence = $false
        }
        '--bayes-prior-alpha' {
            $i++
            $bayesPriorAlpha = [double]$args[$i]
        }
        '--bayes-prior-beta' {
            $i++
            $bayesPriorBeta = [double]$args[$i]
        }
        '--signature-verification-mode' {
            $i++
            $signatureVerificationMode = [string]$args[$i]
        }
        '--kms-public-key-file' {
            $i++
            $kmsPublicKeyFile = [string]$args[$i]
        }
        '--sigstore-certificate-file' {
            $i++
            $sigstoreCertificateFile = [string]$args[$i]
        }
        '--sigstore-certificate-identity' {
            $i++
            $sigstoreCertificateIdentity = [string]$args[$i]
        }
        '--sigstore-oidc-issuer' {
            $i++
            $sigstoreOidcIssuer = [string]$args[$i]
        }
        '--cosign-path' {
            $i++
            $cosignPath = [string]$args[$i]
        }
        '--strict-schema-version' {
            $i++
            $strictSchemaVersion = [string]$args[$i]
        }
        '--trend-window-size' {
            $i++
            $trendWindowSize = [int]$args[$i]
        }
        '--notes' {
            $i++
            $notes = [string]$args[$i]
        }
        '--dry-run' {
            $dryRun = $true
        }
        '-h' {
            Show-Usage
            exit 0
        }
        '--help' {
            Show-Usage
            exit 0
        }
        default {
            throw "Unknown option: $arg"
        }
    }
}

if ([string]::IsNullOrWhiteSpace($simulationId) -or
    [string]::IsNullOrWhiteSpace($tenantId) -or
    [string]::IsNullOrWhiteSpace($rolloutPhase) -or
    [string]::IsNullOrWhiteSpace($dependency) -or
    [string]::IsNullOrWhiteSpace($result) -or
    [string]::IsNullOrWhiteSpace($evidencePath)) {
    throw 'Missing required arguments. Use --help for usage.'
}

if ($dependency -notin @('registry', 'authority')) {
    throw '--dependency must be one of: registry, authority'
}

if ($result -notin @('pass', 'fail')) {
    throw '--result must be one of: pass, fail'
}

if ($rolloutPhase -notin @('r1', 'r2', 'r3', 'r4')) {
    throw '--rollout-phase must be one of: r1, r2, r3, r4'
}

if ($trendWindowSize -le 0) {
    throw '--trend-window-size must be a positive integer'
}

if ($attestationBaselineWindow -le 0) {
    throw '--attestation-baseline-window must be a positive integer'
}

if ($attestationAnomalyThresholdPercent -lt 0 -or
    $attestationAnomalyThresholdPercent -gt 100) {
    throw '--attestation-anomaly-threshold-percent must be an integer between 0 and 100'
}

if ($timeDecayHalfLife -le 0) {
    throw '--time-decay-half-life must be a positive integer'
}

if ($reportingCycleLength -le 0) {
    throw '--reporting-cycle-length must be a positive integer'
}

if ($graphCohortWindow -le 0) {
    throw '--graph-cohort-window must be a positive integer'
}

if ($graphTemporalDecayRate -lt 0) {
    throw '--graph-temporal-decay-rate must be a non-negative number'
}

if ($explainerDriftThreshold -lt 0) {
    throw '--explainer-drift-threshold must be a non-negative integer'
}

if ($confidenceBoundZScore -le 0) {
    throw '--confidence-bound-zscore must be a positive number'
}

if ($bayesPriorAlpha -le 0) {
    throw '--bayes-prior-alpha must be a positive number'
}

if ($bayesPriorBeta -le 0) {
    throw '--bayes-prior-beta must be a positive number'
}

if ([string]::IsNullOrWhiteSpace($policyProfile)) {
    throw '--policy-profile cannot be empty'
}

if (-not (Test-Path -LiteralPath $explainabilityHistoryFile)) {
    Set-Content -LiteralPath $explainabilityHistoryFile -NoNewline -Value
        'timestamp,tenant_id,rollout_phase,dependency,failure_mode,sample_contrib,fail_contrib,context_contrib,persistence_contrib,confidence_score,recommended_overlay,decision'
    Add-Content -LiteralPath $explainabilityHistoryFile -Value ''
}

if ($signatureVerificationMode -notin @('none', 'kms', 'sigstore')) {
    throw '--signature-verification-mode must be one of: none, kms, sigstore'
}

if ($signatureVerificationMode -ne 'none') {
    $requireSignedManifest = $true
}

if ($requireTrustPolicy -or $requireRevocationCheck) {
    $requireSignedManifest = $true
}

if ($requireTrustPolicyAttestation -or $requireRevocationSlo) {
    $requireSignedManifest = $true
}

if (-not (Test-Path -LiteralPath $trendHistoryFile)) {
    Set-Content -LiteralPath $trendHistoryFile -NoNewline -Value
        'timestamp,tenant_id,rollout_phase,dependency,result,simulation_id'
    Add-Content -LiteralPath $trendHistoryFile -Value ''
}

if (-not (Test-Path -LiteralPath $causalHistoryFile)) {
    Set-Content -LiteralPath $causalHistoryFile -NoNewline -Value
        'timestamp,tenant_id,rollout_phase,dependency,failure_mode,result,simulation_id'
    Add-Content -LiteralPath $causalHistoryFile -Value ''
}

if ($enableAttestationDashboard -and
    -not (Test-Path -LiteralPath $attestationTrendHistoryFile)) {
    Set-Content -LiteralPath $attestationTrendHistoryFile -NoNewline -Value
        'timestamp,tenant_id,distribution_id,attestation_status,revocation_slo_status,slo_age_hours,simulation_id'
    Add-Content -LiteralPath $attestationTrendHistoryFile -Value ''
}

$timestamp = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
$dateStamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd')
$sanitizedSimulationId = ($simulationId -replace '[^A-Za-z0-9]', '')
$entryId = "OUT-$dateStamp-$sanitizedSimulationId"

$tenantHistory = @()
$historyRows = Import-Csv -LiteralPath $trendHistoryFile
if ($historyRows) {
    $tenantHistory = $historyRows |
        Where-Object {
            $_.tenant_id -eq $tenantId -and
            $_.dependency -eq $dependency
        } |
        Select-Object -Last $trendWindowSize
}

$trendTotal = $tenantHistory.Count
$trendFailCount = @($tenantHistory |
    Where-Object { $_.result -eq 'fail' }).Count
$trendPassCount = @($tenantHistory |
    Where-Object { $_.result -eq 'pass' }).Count
$trendFailRate = 0
if ($trendTotal -gt 0) {
    $trendFailRate = [int](($trendFailCount * 100) / $trendTotal)
}

$failBaseScore = 45
$passBaseScore = 10
$phaseR1Weight = 5
$phaseR2Weight = 10
$phaseR3Weight = 15
$phaseR4Weight = 20
$dependencyRegistryWeight = 8
$dependencyAuthorityWeight = 12
$trendFailDivisor = 5
$trendPassDivisor = 10

if (-not (Test-Path -LiteralPath $profileWeightsFile)) {
    throw "Missing required profile weights file: $profileWeightsFile"
}

if ($requireSignedManifest -or $manifestFileExplicit) {
    if (-not (Test-Path -LiteralPath $profileManifestFile)) {
        throw "Missing required signed manifest file: $profileManifestFile"
    }

    $manifestFields = @{}
    foreach ($line in (Get-Content -LiteralPath $profileManifestFile)) {
        if ([string]::IsNullOrWhiteSpace($line) -or $line.Trim().StartsWith('#')) {
            continue
        }

        $split = $line.Split('=', 2)
        if ($split.Length -eq 2) {
            $manifestFields[$split[0].Trim()] = $split[1].Trim()
        }
    }

    $requiredManifestFields = @(
        'manifest_version',
        'weights_file',
        'weights_sha256',
        'signature',
        'signed_by',
        'signed_at',
        'key_id'
    )

    foreach ($field in $requiredManifestFields) {
        if (-not $manifestFields.ContainsKey($field) -or
            [string]::IsNullOrWhiteSpace([string]$manifestFields[$field])) {
            throw "Malformed signed manifest: required field '$field' missing in $profileManifestFile"
        }
    }

    $manifestVersion = [string]$manifestFields['manifest_version']
    $manifestWeightsFile = [string]$manifestFields['weights_file']
    $manifestWeightsSha256 = [string]$manifestFields['weights_sha256']
    $manifestSignature = [string]$manifestFields['signature']
    $manifestSignedBy = [string]$manifestFields['signed_by']
    $manifestSignedAt = [string]$manifestFields['signed_at']
    $manifestKeyId = [string]$manifestFields['key_id']
    $manifestSignatureScheme = [string]($manifestFields['signature_scheme'])
    $manifestSignatureFile = [string]($manifestFields['signature_file'])
    $manifestCertificateFile = [string]($manifestFields['certificate_file'])

    if ($manifestWeightsSha256 -notmatch '^[A-Fa-f0-9]{64}$') {
        throw "Malformed signed manifest: weights_sha256 must be 64-char hex in $profileManifestFile"
    }

    if ($manifestWeightsFile -ne $profileWeightsFile) {
        throw "Signed manifest mismatch: weights_file '$manifestWeightsFile' does not match selected file '$profileWeightsFile'"
    }

    $weightsHash =
        (Get-FileHash -LiteralPath $profileWeightsFile -Algorithm SHA256).Hash
    if ($weightsHash.ToLowerInvariant() -ne
        $manifestWeightsSha256.ToLowerInvariant()) {
        throw "Signed manifest integrity failure: weights_sha256 mismatch for $profileWeightsFile"
    }

    if ($signatureVerificationMode -ne 'none') {
        if ([string]::IsNullOrWhiteSpace($manifestSignatureScheme) -or
            [string]::IsNullOrWhiteSpace($manifestSignatureFile)) {
            throw "Malformed signed manifest: signature_scheme and signature_file are required for cryptographic verification"
        }

        if ($manifestSignatureScheme -ne $signatureVerificationMode) {
            throw "Signature verification mode mismatch: requested '$signatureVerificationMode' but manifest declares '$manifestSignatureScheme'"
        }

        if (-not (Test-Path -LiteralPath $manifestSignatureFile)) {
            throw "Missing signature file referenced by manifest: $manifestSignatureFile"
        }

        if ($signatureVerificationMode -eq 'kms') {
            if ([string]::IsNullOrWhiteSpace($kmsPublicKeyFile)) {
                throw '--kms-public-key-file is required for --signature-verification-mode kms'
            }

            if (-not (Test-Path -LiteralPath $kmsPublicKeyFile)) {
                throw "Missing KMS public key file: $kmsPublicKeyFile"
            }

            $kmsCommand =
                "openssl dgst -sha256 -verify `"$kmsPublicKeyFile`" -signature `"$manifestSignatureFile`" `"$profileWeightsFile`""
            $kmsOutput = Invoke-Expression $kmsCommand 2>&1
            if ($LASTEXITCODE -ne 0) {
                throw "Cryptographic signature verification failed for kms mode: $kmsOutput"
            }
        }

        if ($signatureVerificationMode -eq 'sigstore') {
            if ([string]::IsNullOrWhiteSpace($sigstoreCertificateFile) -or
                [string]::IsNullOrWhiteSpace($sigstoreCertificateIdentity) -or
                [string]::IsNullOrWhiteSpace($sigstoreOidcIssuer)) {
                throw '--sigstore-certificate-file, --sigstore-certificate-identity, and --sigstore-oidc-issuer are required for sigstore mode'
            }

            if (-not (Test-Path -LiteralPath $sigstoreCertificateFile)) {
                throw "Missing sigstore certificate file: $sigstoreCertificateFile"
            }

            $sigstoreCommand =
                "$cosignPath verify-blob --signature `"$manifestSignatureFile`" --certificate `"$sigstoreCertificateFile`" --certificate-identity `"$sigstoreCertificateIdentity`" --certificate-oidc-issuer `"$sigstoreOidcIssuer`" `"$profileWeightsFile`""
            $sigstoreOutput = Invoke-Expression $sigstoreCommand 2>&1
            if ($LASTEXITCODE -ne 0) {
                throw "Cryptographic signature verification failed for sigstore mode: $sigstoreOutput"
            }
        }
    }

    if ($requireTrustPolicyAttestation) {
        if (-not (Test-Path -LiteralPath $trustPolicyAttestationFile)) {
            throw "Missing trust-policy attestation file: $trustPolicyAttestationFile"
        }

        $attestationFields = @{}
        foreach ($line in (Get-Content -LiteralPath $trustPolicyAttestationFile)) {
            if ([string]::IsNullOrWhiteSpace($line) -or
                $line.Trim().StartsWith('#')) {
                continue
            }

            $split = $line.Split('=', 2)
            if ($split.Length -eq 2) {
                $attestationFields[$split[0].Trim()] = $split[1].Trim()
            }
        }

        $requiredAttestationFields = @(
            'distribution_id',
            'published_at',
            'attestation_status',
            'signer',
            'artifact_digest'
        )

        foreach ($field in $requiredAttestationFields) {
            if (-not $attestationFields.ContainsKey($field) -or
                [string]::IsNullOrWhiteSpace([string]$attestationFields[$field])) {
                throw "Malformed trust-policy attestation file: required field '$field' missing in $trustPolicyAttestationFile"
            }
        }

        $attestationDistributionId =
            [string]$attestationFields['distribution_id']
        $attestationPublishedAt = [string]$attestationFields['published_at']
        $attestationStatus = [string]$attestationFields['attestation_status']
        $attestationSigner = [string]$attestationFields['signer']
        $attestationArtifactDigest =
            [string]$attestationFields['artifact_digest']

        if ($attestationStatus -notin @('published', 'verified')) {
            throw "Trust-policy attestation status invalid: expected published|verified in $trustPolicyAttestationFile"
        }

        if (-not [string]::IsNullOrWhiteSpace($trustPolicyDistributionId) -and
            $attestationDistributionId -ne $trustPolicyDistributionId) {
            throw "Trust-policy attestation mismatch: distribution_id '$attestationDistributionId' does not match trust policy '$trustPolicyDistributionId'"
        }

        if ($attestationArtifactDigest -notmatch '^[A-Fa-f0-9]{64}$') {
            throw "Malformed trust-policy attestation file: artifact_digest must be 64-char hex"
        }

        $trustPolicyHash =
            (Get-FileHash -LiteralPath $trustPolicyFile -Algorithm SHA256).Hash
        if ($trustPolicyHash.ToLowerInvariant() -ne
            $attestationArtifactDigest.ToLowerInvariant()) {
            throw "Trust-policy attestation digest mismatch for $trustPolicyFile"
        }
    }

    if ($requireTrustPolicy) {
        if (-not (Test-Path -LiteralPath $trustPolicyFile)) {
            throw "Missing trust-policy file: $trustPolicyFile"
        }

        $trustFields = @{}
        foreach ($line in (Get-Content -LiteralPath $trustPolicyFile)) {
            if ([string]::IsNullOrWhiteSpace($line) -or
                $line.Trim().StartsWith('#')) {
                continue
            }

            $split = $line.Split('=', 2)
            if ($split.Length -eq 2) {
                $trustFields[$split[0].Trim()] = $split[1].Trim()
            }
        }

        $requiredTrustFields = @(
            'distribution_id',
            'distributed_at',
            'federation_scope',
            'active_key_ids'
        )

        foreach ($field in $requiredTrustFields) {
            if (-not $trustFields.ContainsKey($field) -or
                [string]::IsNullOrWhiteSpace([string]$trustFields[$field])) {
                throw "Malformed trust-policy file: required field '$field' missing in $trustPolicyFile"
            }
        }

        $trustPolicyDistributionId = [string]$trustFields['distribution_id']
        $trustPolicyDistributedAt = [string]$trustFields['distributed_at']
        $trustPolicyFederationScope = [string]$trustFields['federation_scope']
        $activeKeysRaw = [string]$trustFields['active_key_ids']
        $revokedKeysRaw = [string]($trustFields['revoked_key_ids'])
        $maxAgeDaysRaw = [string]($trustFields['max_distribution_age_days'])

        $activeKeys = @($activeKeysRaw -split '[,;]' |
            ForEach-Object { $_.Trim() } |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        $revokedKeys = @($revokedKeysRaw -split '[,;]' |
            ForEach-Object { $_.Trim() } |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) })

        $trustPolicyActiveKeyCount = $activeKeys.Count
        $trustPolicyRevokedKeyCount = $revokedKeys.Count

        if ($manifestKeyId -notin $activeKeys) {
            throw "Trust-policy violation: key_id '$manifestKeyId' is not active in $trustPolicyFile"
        }

        if ($manifestKeyId -in $revokedKeys) {
            throw "Trust-policy violation: key_id '$manifestKeyId' is explicitly revoked in $trustPolicyFile"
        }

        if (-not [string]::IsNullOrWhiteSpace($maxAgeDaysRaw)) {
            [int]$maxAgeDays = 0
            if (-not [int]::TryParse($maxAgeDaysRaw, [ref]$maxAgeDays)) {
                throw "Malformed trust-policy file: max_distribution_age_days must be numeric in $trustPolicyFile"
            }

            if ($maxAgeDays -gt 0) {
                [DateTime]$distributedAtValue = [DateTime]::MinValue
                if (-not [DateTime]::TryParse($trustPolicyDistributedAt, [ref]$distributedAtValue)) {
                    throw "Malformed trust-policy file: distributed_at is invalid in $trustPolicyFile"
                }

                $ageDays = [int]((New-TimeSpan -Start $distributedAtValue.ToUniversalTime() -End (Get-Date).ToUniversalTime()).TotalDays)
                if ($ageDays -gt $maxAgeDays) {
                    throw "Trust-policy distribution is stale: age $ageDays days exceeds $maxAgeDays in $trustPolicyFile"
                }
            }
        }
    }

    if ($requireRevocationCheck) {
        if (-not (Test-Path -LiteralPath $revocationListFile)) {
            throw "Missing revocation list file: $revocationListFile"
        }

        $revocationRows = Import-Csv -LiteralPath $revocationListFile
        if (-not $revocationRows) {
            throw "Malformed revocation list file (empty or invalid CSV): $revocationListFile"
        }

        $revokedEntry = $revocationRows |
            Where-Object {
                $_.key_id -eq $manifestKeyId -and
                ([string]$_.status).ToLowerInvariant() -eq 'revoked'
            } |
            Select-Object -First 1

        if ($revokedEntry) {
            throw "Revocation check failed: key_id '$manifestKeyId' is revoked in $revocationListFile"
        }
    }

    if ($requireRevocationSlo) {
        if (-not (Test-Path -LiteralPath $revocationSloFile)) {
            throw "Missing revocation SLO file: $revocationSloFile"
        }

        $sloFields = @{}
        foreach ($line in (Get-Content -LiteralPath $revocationSloFile)) {
            if ([string]::IsNullOrWhiteSpace($line) -or
                $line.Trim().StartsWith('#')) {
                continue
            }

            $split = $line.Split('=', 2)
            if ($split.Length -eq 2) {
                $sloFields[$split[0].Trim()] = $split[1].Trim()
            }
        }

        $requiredSloFields = @(
            'max_propagation_hours',
            'last_propagation_check_at',
            'status'
        )

        foreach ($field in $requiredSloFields) {
            if (-not $sloFields.ContainsKey($field) -or
                [string]::IsNullOrWhiteSpace([string]$sloFields[$field])) {
                throw "Malformed revocation SLO file: required field '$field' missing in $revocationSloFile"
            }
        }

        $revocationSloMaxHours = [string]$sloFields['max_propagation_hours']
        $revocationSloLastCheckAt =
            [string]$sloFields['last_propagation_check_at']
        $revocationSloStatus = [string]$sloFields['status']

        [int]$maxHours = 0
        if (-not [int]::TryParse($revocationSloMaxHours, [ref]$maxHours)) {
            throw "Malformed revocation SLO file: max_propagation_hours must be numeric in $revocationSloFile"
        }

        if ($revocationSloStatus -notin @('healthy', 'warning')) {
            throw "Revocation SLO status invalid: expected healthy|warning in $revocationSloFile"
        }

        [DateTime]$lastCheckValue = [DateTime]::MinValue
        if (-not [DateTime]::TryParse($revocationSloLastCheckAt,
                [ref]$lastCheckValue)) {
            throw "Malformed revocation SLO file: last_propagation_check_at is invalid in $revocationSloFile"
        }

        $ageHours = [int]((New-TimeSpan -Start $lastCheckValue.ToUniversalTime() -End (Get-Date).ToUniversalTime()).TotalHours)
        if ($ageHours -gt $maxHours) {
            throw "Revocation propagation SLO breach: age $ageHours hours exceeds $maxHours in $revocationSloFile"
        }
    }

    if ($enableAttestationDashboard) {
        if (-not (Test-Path -LiteralPath $attestationTrendHistoryFile)) {
            Set-Content -LiteralPath $attestationTrendHistoryFile -NoNewline -Value
                'timestamp,tenant_id,distribution_id,attestation_status,revocation_slo_status,slo_age_hours,simulation_id'
            Add-Content -LiteralPath $attestationTrendHistoryFile -Value ''
        }

        $distributionValue = if ($attestationDistributionId) {
            $attestationDistributionId
        } elseif ($trustPolicyDistributionId) {
            $trustPolicyDistributionId
        } else {
            'unknown'
        }

        $attestationStatusValue = if ($attestationStatus) {
            $attestationStatus
        } else {
            'unknown'
        }

        $sloStatusValue = if ($revocationSloStatus) {
            $revocationSloStatus
        } else {
            'unknown'
        }

        $sloAgeValue = if ($ageHours -or $ageHours -eq 0) {
            $ageHours
        } else {
            0
        }

        $attestationHistoryRows = Import-Csv -LiteralPath $attestationTrendHistoryFile
        $baselineRows = @($attestationHistoryRows |
            Where-Object { $_.tenant_id -eq $tenantId } |
            Select-Object -Last $attestationBaselineWindow)

        $attestationBaselineSamples = $baselineRows.Count
        $attestationBaselineAlertCount = @($baselineRows |
            Where-Object {
                $_.attestation_status -ne 'verified' -or
                $_.revocation_slo_status -eq 'warning'
            }).Count

        $attestationAnomalyPercent = 0
        if ($attestationBaselineSamples -gt 0) {
            $attestationAnomalyPercent = [int](
                ($attestationBaselineAlertCount * 100) /
                $attestationBaselineSamples
            )
        }

        $weightedAnomalyPercent = $attestationAnomalyPercent
        $timeDecayHalfLifeEffective = $timeDecayHalfLife
        $timeDecaySource = 'disabled'
        $recurrenceRatioPercent = 0
        $recurrenceTunedHalfLife = $timeDecayHalfLife
        $recurrenceTuningSource = 'disabled'
        $seasonalPhase = 'not-applied'
        $seasonalMultiplier = 1.0
        $seasonalTunedHalfLife = $timeDecayHalfLife
        $seasonalDecompositionSource = 'disabled'
        $seasonalOverlayName = 'none'
        $seasonalOverlayMultiplier = 1.0
        $seasonalOverlaySource = 'disabled'
        $causalClusterKey = 'none'
        $causalClusterSampleCount = 0
        $causalClusterFailCount = 0
        $causalClusterFailPercent = 0
        $causalClusterSuggestedOverlay = 'none'
        $causalClusterSuggestedMultiplier = 1.00
        $causalClusterSource = 'disabled'
        $causalConfidenceScore = 0
        $causalConfidenceThreshold = 60
        $causalConfidenceSource = 'disabled'
        $causalGraphNode = 'none'
        $causalGraphEdge = 'none'

        if ($enableTimeDecayWeighting) {
            $timeDecaySource = 'cli'

            if (Test-Path -LiteralPath $timeDecayPolicyFile) {
                $decayFields = @{}
                foreach ($line in (Get-Content -LiteralPath $timeDecayPolicyFile)) {
                    if ([string]::IsNullOrWhiteSpace($line) -or
                        $line.Trim().StartsWith('#')) {
                        continue
                    }

                    $split = $line.Split('=', 2)
                    if ($split.Length -eq 2) {
                        $decayFields[$split[0].Trim()] = $split[1].Trim()
                    }
                }

                if ($decayFields.ContainsKey('half_life_samples')) {
                    [int]$policyHalfLife = 0
                    if ([int]::TryParse(
                            [string]$decayFields['half_life_samples'],
                            [ref]$policyHalfLife) -and
                        $policyHalfLife -gt 0) {
                        $timeDecayHalfLifeEffective = $policyHalfLife
                        $timeDecaySource = 'policy'
                    } elseif ($requireTimeDecayPolicy) {
                        throw "Time-decay policy failed: half_life_samples missing or invalid in $timeDecayPolicyFile"
                    }
                } elseif ($requireTimeDecayPolicy) {
                    throw "Time-decay policy failed: half_life_samples missing or invalid in $timeDecayPolicyFile"
                }
            } elseif ($requireTimeDecayPolicy) {
                throw "Time-decay policy failed: missing file $timeDecayPolicyFile"
            }

            if ($enableRecurrenceAutoTuning) {
                $recurrenceTuningSource = 'static'
                [int]$recurrenceMin = 2
                [int]$recurrenceMax = 20
                [double]$recurrenceMultiplier = 2.0

                if (Test-Path -LiteralPath $recurrenceTuningPolicyFile) {
                    $recFields = @{}
                    foreach ($line in (Get-Content -LiteralPath $recurrenceTuningPolicyFile)) {
                        if ([string]::IsNullOrWhiteSpace($line) -or
                            $line.Trim().StartsWith('#')) {
                            continue
                        }

                        $split = $line.Split('=', 2)
                        if ($split.Length -eq 2) {
                            $recFields[$split[0].Trim()] = $split[1].Trim()
                        }
                    }

                    [int]$tmpMin = 0
                    [int]$tmpMax = 0
                    [double]$tmpMult = 0

                    if ($recFields.ContainsKey('min_half_life_samples') -and
                        [int]::TryParse(
                            [string]$recFields['min_half_life_samples'],
                            [ref]$tmpMin) -and
                        $tmpMin -gt 0) {
                        $recurrenceMin = $tmpMin
                    } elseif ($requireRecurrenceTuningPolicy) {
                        throw "Recurrence tuning policy failed: min_half_life_samples missing/invalid in $recurrenceTuningPolicyFile"
                    }

                    if ($recFields.ContainsKey('max_half_life_samples') -and
                        [int]::TryParse(
                            [string]$recFields['max_half_life_samples'],
                            [ref]$tmpMax) -and
                        $tmpMax -ge $recurrenceMin) {
                        $recurrenceMax = $tmpMax
                    } elseif ($requireRecurrenceTuningPolicy) {
                        throw "Recurrence tuning policy failed: max_half_life_samples missing/invalid in $recurrenceTuningPolicyFile"
                    }

                    if ($recFields.ContainsKey('fail_multiplier') -and
                        [double]::TryParse(
                            [string]$recFields['fail_multiplier'],
                            [ref]$tmpMult) -and
                        $tmpMult -gt 0) {
                        $recurrenceMultiplier = $tmpMult
                    } elseif ($requireRecurrenceTuningPolicy) {
                        throw "Recurrence tuning policy failed: fail_multiplier missing/invalid in $recurrenceTuningPolicyFile"
                    }

                    $recurrenceTuningSource = 'policy'
                } elseif ($requireRecurrenceTuningPolicy) {
                    throw "Recurrence tuning policy failed: missing file $recurrenceTuningPolicyFile"
                }

                if ($attestationBaselineSamples -gt 0) {
                    $recurrenceAlerts = @($baselineRows | Where-Object {
                            $_.attestation_status -ne 'verified' -or
                            $_.revocation_slo_status -eq 'warning'
                        }).Count
                    $recurrenceRatioPercent =
                        [int](($recurrenceAlerts * 100) / $attestationBaselineSamples)
                }

                $tunedRaw =
                    $timeDecayHalfLifeEffective +
                    (($recurrenceRatioPercent / 100.0) *
                        $recurrenceMultiplier * $timeDecayHalfLifeEffective)
                $tunedBounded = [Math]::Max($recurrenceMin,
                    [Math]::Min($recurrenceMax, [int]$tunedRaw))
                $timeDecayHalfLifeEffective = $tunedBounded
                $recurrenceTunedHalfLife = $tunedBounded
            }

            if ($enableSeasonalRecurrenceDecomposition) {
                $seasonalDecompositionSource = 'static'
                [int]$cycleLengthEffective = $reportingCycleLength
                [double]$cycleStartMultiplier = 1.20
                [double]$cycleMidMultiplier = 1.00
                [double]$cycleEndMultiplier = 0.85
                [int]$seasonalMinHalfLife = 2
                [int]$seasonalMaxHalfLife = 30

                if (Test-Path -LiteralPath $seasonalDecompositionPolicyFile) {
                    $seasonalFields = @{}
                    foreach ($line in (Get-Content -LiteralPath $seasonalDecompositionPolicyFile)) {
                        if ([string]::IsNullOrWhiteSpace($line) -or
                            $line.Trim().StartsWith('#')) {
                            continue
                        }
                        $split = $line.Split('=', 2)
                        if ($split.Length -eq 2) {
                            $seasonalFields[$split[0].Trim()] = $split[1].Trim()
                        }
                    }

                    [int]$tmpCycle = 0
                    [double]$tmpStart = 0
                    [double]$tmpMid = 0
                    [double]$tmpEnd = 0
                    [int]$tmpMinHalf = 0
                    [int]$tmpMaxHalf = 0

                    if ($seasonalFields.ContainsKey('reporting_cycle_length') -and
                        [int]::TryParse([string]$seasonalFields['reporting_cycle_length'], [ref]$tmpCycle) -and
                        $tmpCycle -gt 0) {
                        $cycleLengthEffective = $tmpCycle
                    } elseif ($requireSeasonalDecompositionPolicy) {
                        throw "Seasonal decomposition policy failed: reporting_cycle_length missing/invalid in $seasonalDecompositionPolicyFile"
                    }

                    if ($seasonalFields.ContainsKey('cycle_start_multiplier') -and
                        [double]::TryParse([string]$seasonalFields['cycle_start_multiplier'], [ref]$tmpStart) -and
                        $tmpStart -gt 0) {
                        $cycleStartMultiplier = $tmpStart
                    } elseif ($requireSeasonalDecompositionPolicy) {
                        throw "Seasonal decomposition policy failed: cycle_start_multiplier missing/invalid in $seasonalDecompositionPolicyFile"
                    }

                    if ($seasonalFields.ContainsKey('cycle_mid_multiplier') -and
                        [double]::TryParse([string]$seasonalFields['cycle_mid_multiplier'], [ref]$tmpMid) -and
                        $tmpMid -gt 0) {
                        $cycleMidMultiplier = $tmpMid
                    } elseif ($requireSeasonalDecompositionPolicy) {
                        throw "Seasonal decomposition policy failed: cycle_mid_multiplier missing/invalid in $seasonalDecompositionPolicyFile"
                    }

                    if ($seasonalFields.ContainsKey('cycle_end_multiplier') -and
                        [double]::TryParse([string]$seasonalFields['cycle_end_multiplier'], [ref]$tmpEnd) -and
                        $tmpEnd -gt 0) {
                        $cycleEndMultiplier = $tmpEnd
                    } elseif ($requireSeasonalDecompositionPolicy) {
                        throw "Seasonal decomposition policy failed: cycle_end_multiplier missing/invalid in $seasonalDecompositionPolicyFile"
                    }

                    if ($seasonalFields.ContainsKey('seasonal_min_half_life_samples') -and
                        [int]::TryParse([string]$seasonalFields['seasonal_min_half_life_samples'], [ref]$tmpMinHalf) -and
                        $tmpMinHalf -gt 0) {
                        $seasonalMinHalfLife = $tmpMinHalf
                    } elseif ($requireSeasonalDecompositionPolicy) {
                        throw "Seasonal decomposition policy failed: seasonal_min_half_life_samples missing/invalid in $seasonalDecompositionPolicyFile"
                    }

                    if ($seasonalFields.ContainsKey('seasonal_max_half_life_samples') -and
                        [int]::TryParse([string]$seasonalFields['seasonal_max_half_life_samples'], [ref]$tmpMaxHalf) -and
                        $tmpMaxHalf -ge $seasonalMinHalfLife) {
                        $seasonalMaxHalfLife = $tmpMaxHalf
                    } elseif ($requireSeasonalDecompositionPolicy) {
                        throw "Seasonal decomposition policy failed: seasonal_max_half_life_samples missing/invalid in $seasonalDecompositionPolicyFile"
                    }

                    $seasonalDecompositionSource = 'policy'
                } elseif ($requireSeasonalDecompositionPolicy) {
                    throw "Seasonal decomposition policy failed: missing file $seasonalDecompositionPolicyFile"
                }

                $cyclePosition = 0
                if ($cycleLengthEffective -gt 0) {
                    $cyclePosition = $attestationBaselineSamples % $cycleLengthEffective
                }
                $cycleThird = [Math]::Max(1, [int]($cycleLengthEffective / 3))

                if ($cyclePosition -lt $cycleThird) {
                    $seasonalPhase = 'cycle_start'
                    $seasonalMultiplier = $cycleStartMultiplier
                } elseif ($cyclePosition -lt ($cycleThird * 2)) {
                    $seasonalPhase = 'cycle_mid'
                    $seasonalMultiplier = $cycleMidMultiplier
                } else {
                    $seasonalPhase = 'cycle_end'
                    $seasonalMultiplier = $cycleEndMultiplier
                }

                if ($enableSeasonalOverlayTuning) {
                    $seasonalOverlaySource = 'static'
                    if (Test-Path -LiteralPath $seasonalOverlayPolicyFile) {
                        $overlayRows = Import-Csv -LiteralPath $seasonalOverlayPolicyFile
                        $overlayRow = $overlayRows | Where-Object {
                            ($_.cycle_phase -eq $seasonalPhase -or $_.cycle_phase -eq '*') -and
                            ($_.cycle_index -eq [string]$cyclePosition -or $_.cycle_index -eq '*')
                        } | Select-Object -First 1

                        if ($overlayRow) {
                            [double]$overlayValue = 0
                            if ([double]::TryParse([string]$overlayRow.overlay_multiplier, [ref]$overlayValue) -and
                                $overlayValue -gt 0) {
                                $seasonalOverlayName = if ([string]::IsNullOrWhiteSpace([string]$overlayRow.overlay_name)) { 'unnamed-overlay' } else { [string]$overlayRow.overlay_name }
                                $seasonalOverlayMultiplier = $overlayValue
                                $seasonalOverlaySource = 'policy'
                                $seasonalMultiplier = $seasonalMultiplier * $seasonalOverlayMultiplier
                            } elseif ($requireSeasonalOverlayPolicy) {
                                throw "Seasonal overlay policy failed: invalid overlay multiplier in $seasonalOverlayPolicyFile"
                            }
                        } elseif ($requireSeasonalOverlayPolicy) {
                            throw "Seasonal overlay policy failed: no overlay match for phase=$seasonalPhase cycle_index=$cyclePosition in $seasonalOverlayPolicyFile"
                        }
                    } elseif ($requireSeasonalOverlayPolicy) {
                        throw "Seasonal overlay policy failed: missing file $seasonalOverlayPolicyFile"
                    }
                }

                $seasonalRaw = $timeDecayHalfLifeEffective * $seasonalMultiplier
                $seasonalBounded = [Math]::Max($seasonalMinHalfLife,
                    [Math]::Min($seasonalMaxHalfLife, [int]$seasonalRaw))
                $timeDecayHalfLifeEffective = $seasonalBounded
                $seasonalTunedHalfLife = $seasonalBounded
            }

            if ($attestationBaselineSamples -gt 0) {
                [double]$weightedAlertSum = 0
                [double]$weightedTotalSum = 0
                $index = 0
                foreach ($row in $baselineRows) {
                    $age = $attestationBaselineSamples - $index - 1
                    $weight = [Math]::Pow(0.5,
                        ([double]$age / [double]$timeDecayHalfLifeEffective))

                    $weightedTotalSum += $weight
                    if ($row.attestation_status -ne 'verified' -or
                        $row.revocation_slo_status -eq 'warning') {
                        $weightedAlertSum += $weight
                    }
                    $index++
                }

                if ($weightedTotalSum -gt 0) {
                    $weightedAnomalyPercent =
                        [int](($weightedAlertSum * 100) / $weightedTotalSum)
                }
            }

            if ($enableCausalClustering) {
                $causalClusterSource = 'static'
                [int]$clusterMinSamples = 3
                [int]$clusterFailThresholdPercent = 50
                [double]$clusterHighMultiplier = 1.15

                if (Test-Path -LiteralPath $causalClusteringPolicyFile) {
                    $clusterFields = @{}
                    foreach ($line in (Get-Content -LiteralPath $causalClusteringPolicyFile)) {
                        if ([string]::IsNullOrWhiteSpace($line) -or
                            $line.Trim().StartsWith('#')) {
                            continue
                        }
                        $split = $line.Split('=', 2)
                        if ($split.Length -eq 2) {
                            $clusterFields[$split[0].Trim()] = $split[1].Trim()
                        }
                    }

                    [int]$tmpMin = 0
                    [int]$tmpFail = 0
                    [double]$tmpMult = 0
                    if ($clusterFields.ContainsKey('min_samples') -and
                        [int]::TryParse([string]$clusterFields['min_samples'], [ref]$tmpMin) -and
                        $tmpMin -gt 0) {
                        $clusterMinSamples = $tmpMin
                    }
                    if ($clusterFields.ContainsKey('fail_threshold_percent') -and
                        [int]::TryParse([string]$clusterFields['fail_threshold_percent'], [ref]$tmpFail) -and
                        $tmpFail -ge 0 -and $tmpFail -le 100) {
                        $clusterFailThresholdPercent = $tmpFail
                    }
                    if ($clusterFields.ContainsKey('high_impact_multiplier') -and
                        [double]::TryParse([string]$clusterFields['high_impact_multiplier'], [ref]$tmpMult) -and
                        $tmpMult -gt 0) {
                        $clusterHighMultiplier = $tmpMult
                    }
                    $causalClusterSource = 'policy'
                } elseif ($requireCausalClusteringPolicy) {
                    throw "Causal clustering policy failed: missing file $causalClusteringPolicyFile"
                }

                $effectiveFailureMode = if ([string]::IsNullOrWhiteSpace($failureMode)) { 'unspecified' } else { $failureMode }
                $causalClusterKey = "$dependency|$rolloutPhase|$effectiveFailureMode"

                $causalRows = Import-Csv -LiteralPath $causalHistoryFile |
                    Where-Object {
                        $_.tenant_id -eq $tenantId -and
                        $_.dependency -eq $dependency -and
                        $_.rollout_phase -eq $rolloutPhase -and
                        $_.failure_mode -eq $effectiveFailureMode
                    } |
                    Select-Object -Last $attestationBaselineWindow

                $causalClusterSampleCount = @($causalRows).Count
                $causalClusterFailCount = @($causalRows | Where-Object { $_.result -eq 'fail' }).Count
                if ($causalClusterSampleCount -gt 0) {
                    $causalClusterFailPercent =
                        [int](($causalClusterFailCount * 100) / $causalClusterSampleCount)
                }

                if ($causalClusterSampleCount -ge $clusterMinSamples -and
                    $causalClusterFailPercent -ge $clusterFailThresholdPercent) {
                    $causalClusterSuggestedOverlay = "suggested-$dependency-$rolloutPhase"
                    $causalClusterSuggestedMultiplier = $clusterHighMultiplier
                }

                if ($enableConfidenceCausalGraphing) {
                    $causalConfidenceSource = 'static'
                    [double]$sampleWeight = 0.4
                    [double]$failWeight = 0.5
                    [double]$contextWeight = 0.1
                    [double]$edgePersistenceWeight = 0.2
                    [int]$causalConfidenceThreshold = 60
                    $edgePersistenceScore = 0
                    $edgePersistencePercent = 0
                    $graphTemporalSource = 'static'

                    if (Test-Path -LiteralPath $causalGraphingPolicyFile) {
                        $graphFields = @{}
                        foreach ($line in (Get-Content -LiteralPath $causalGraphingPolicyFile)) {
                            if ([string]::IsNullOrWhiteSpace($line) -or
                                $line.Trim().StartsWith('#')) {
                                continue
                            }
                            $split = $line.Split('=', 2)
                            if ($split.Length -eq 2) {
                                $graphFields[$split[0].Trim()] = $split[1].Trim()
                            }
                        }

                        [double]$tmpSw = 0
                        [double]$tmpFw = 0
                        [double]$tmpCw = 0
                        [double]$tmpPw = 0
                        [int]$tmpTh = 0
                        [int]$tmpGw = 0
                        [double]$tmpGd = 0
                        if ($graphFields.ContainsKey('sample_weight') -and
                            [double]::TryParse([string]$graphFields['sample_weight'], [ref]$tmpSw) -and
                            $tmpSw -ge 0) { $sampleWeight = $tmpSw }
                        if ($graphFields.ContainsKey('fail_weight') -and
                            [double]::TryParse([string]$graphFields['fail_weight'], [ref]$tmpFw) -and
                            $tmpFw -ge 0) { $failWeight = $tmpFw }
                        if ($graphFields.ContainsKey('context_weight') -and
                            [double]::TryParse([string]$graphFields['context_weight'], [ref]$tmpCw) -and
                            $tmpCw -ge 0) { $contextWeight = $tmpCw }
                        if ($graphFields.ContainsKey('edge_persistence_weight') -and
                            [double]::TryParse([string]$graphFields['edge_persistence_weight'], [ref]$tmpPw) -and
                            $tmpPw -ge 0) { $edgePersistenceWeight = $tmpPw }
                        if ($graphFields.ContainsKey('recommendation_threshold') -and
                            [int]::TryParse([string]$graphFields['recommendation_threshold'], [ref]$tmpTh) -and
                            $tmpTh -ge 0 -and $tmpTh -le 100) { $causalConfidenceThreshold = $tmpTh }
                        if ($graphFields.ContainsKey('cohort_window') -and
                            [int]::TryParse([string]$graphFields['cohort_window'], [ref]$tmpGw) -and
                            $tmpGw -gt 0) { $graphCohortWindow = $tmpGw }
                        if ($graphFields.ContainsKey('temporal_decay_rate') -and
                            [double]::TryParse([string]$graphFields['temporal_decay_rate'], [ref]$tmpGd) -and
                            $tmpGd -ge 0) { $graphTemporalDecayRate = $tmpGd }
                        $causalConfidenceSource = 'policy'
                        $graphTemporalSource = 'policy'
                    } elseif ($requireCausalGraphingPolicy) {
                        throw "Causal graphing policy failed: missing file $causalGraphingPolicyFile"
                    }

                    $sampleConfidence = 0
                    if ($attestationBaselineWindow -gt 0) {
                        $sampleConfidence = [int](($causalClusterSampleCount * 100) / $attestationBaselineWindow)
                    }
                    if ($sampleConfidence -gt 100) { $sampleConfidence = 100 }

                    $contextScore = 0
                    if ($seasonalOverlaySource -eq 'policy') {
                        $contextScore = 100
                    } elseif ($seasonalDecompositionSource -eq 'policy') {
                        $contextScore = 70
                    } elseif ($timeDecaySource -eq 'policy') {
                        $contextScore = 40
                    }

                    if ($enableGraphEdgePersistence) {
                        $persistRows = Import-Csv -LiteralPath $causalHistoryFile |
                            Where-Object {
                                $_.tenant_id -eq $tenantId -and
                                $_.dependency -eq $dependency -and
                                $_.rollout_phase -eq $rolloutPhase -and
                                $_.failure_mode -eq $effectiveFailureMode
                            } |
                            Select-Object -Last $graphCohortWindow

                        [double]$persistWeightedTotal = 0
                        [double]$persistWeightedFail = 0
                        $pIndex = 0
                        foreach ($row in $persistRows) {
                            $pWeight = [Math]::Exp(-1 * $graphTemporalDecayRate * $pIndex)
                            $persistWeightedTotal += $pWeight
                            if ($row.result -eq 'fail') {
                                $persistWeightedFail += $pWeight
                            }
                            $pIndex++
                        }

                        if ($persistWeightedTotal -gt 0) {
                            $edgePersistencePercent =
                                [int](($persistWeightedFail * 100) / $persistWeightedTotal)
                        }
                        $edgePersistenceScore = $edgePersistencePercent
                    } else {
                        $graphTemporalSource = 'disabled'
                    }

                    $rawScore = ($sampleConfidence * $sampleWeight) +
                        ($causalClusterFailPercent * $failWeight) +
                        ($contextScore * $contextWeight) +
                        ($edgePersistenceScore * $edgePersistenceWeight)
                    $explainSampleContrib = [int]($sampleConfidence * $sampleWeight)
                    $explainFailContrib = [int]($causalClusterFailPercent * $failWeight)
                    $explainContextContrib = [int]($contextScore * $contextWeight)
                    $explainPersistContrib = [int]($edgePersistenceScore * $edgePersistenceWeight)
                    $causalConfidenceScore =
                        [int]([Math]::Max(0, [Math]::Min(100, $rawScore)))
                    $causalGraphNode = "$causalClusterKey|confidence=$causalConfidenceScore"

                    if ($causalConfidenceScore -lt $causalConfidenceThreshold) {
                        $causalClusterSuggestedOverlay = 'none'
                        $causalClusterSuggestedMultiplier = 1.00
                    }

                    $causalGraphEdge = "$causalClusterKey -> $causalClusterSuggestedOverlay (score=$causalConfidenceScore,persistence=$edgePersistenceScore)"
                }
            }
        }

        $calibratedAttestationThresholdPercent =
            $attestationAnomalyThresholdPercent
        $tenantCriticalityTier = 'not-configured'
        $adaptiveThresholdSource = 'static'

        if ($enableAdaptiveThresholdCalibration) {
            if (-not (Test-Path -LiteralPath $tenantCriticalityFile)) {
                if ($requireAdaptiveThresholdPolicy) {
                    throw "Adaptive threshold calibration failed: missing tenant criticality file $tenantCriticalityFile"
                }
            } elseif (-not (Test-Path -LiteralPath $adaptiveThresholdPolicyFile)) {
                if ($requireAdaptiveThresholdPolicy) {
                    throw "Adaptive threshold calibration failed: missing threshold policy file $adaptiveThresholdPolicyFile"
                }
            } else {
                $tierRows = Import-Csv -LiteralPath $tenantCriticalityFile
                $tenantTierRow = $tierRows |
                    Where-Object { $_.tenant_id -eq $tenantId } |
                    Select-Object -First 1

                if ($tenantTierRow) {
                    $tenantCriticalityTier =
                        [string]$tenantTierRow.criticality_tier

                    $policyRows =
                        Import-Csv -LiteralPath $adaptiveThresholdPolicyFile
                    $tierPolicy = $policyRows |
                        Where-Object {
                            $_.criticality_tier -eq $tenantCriticalityTier
                        } |
                        Select-Object -First 1

                    if ($tierPolicy) {
                        [int]$tierThreshold = 0
                        if ([int]::TryParse(
                                [string]$tierPolicy.anomaly_threshold_percent,
                                [ref]$tierThreshold)) {
                            $calibratedAttestationThresholdPercent =
                                $tierThreshold
                            $adaptiveThresholdSource = 'tier-policy'
                        } elseif ($requireAdaptiveThresholdPolicy) {
                            throw "Adaptive threshold calibration failed: non-numeric threshold for tier '$tenantCriticalityTier'"
                        }
                    } elseif ($requireAdaptiveThresholdPolicy) {
                        throw "Adaptive threshold calibration failed: unresolved threshold for tier '$tenantCriticalityTier'"
                    }
                } elseif ($requireAdaptiveThresholdPolicy) {
                    throw "Adaptive threshold calibration failed: unresolved tenant criticality tier for '$tenantId'"
                }
            }
        }

        $effectiveAnomalyPercent = if ($enableTimeDecayWeighting) {
            $weightedAnomalyPercent
        } else {
            $attestationAnomalyPercent
        }

        if ($requireAttestationBaselineGate -and
            $effectiveAnomalyPercent -gt
            $calibratedAttestationThresholdPercent) {
            throw "Attestation anomaly baseline breach: $effectiveAnomalyPercent% exceeds $calibratedAttestationThresholdPercent% for tenant $tenantId"
        }

        $attestationTrendRecord =
            "$timestamp,$tenantId,$distributionValue,$attestationStatusValue,$sloStatusValue,$sloAgeValue,$simulationId"

        if ($dryRun) {
            Write-Host "[DRY-RUN] Would append attestation trend record to ${attestationTrendHistoryFile}:"
            Write-Host $attestationTrendRecord
        } else {
            Add-Content -LiteralPath $attestationTrendHistoryFile -Value $attestationTrendRecord
        }

        $dashboardEntry = @"
## [ATTN-$entryId] tenant_attestation_dashboard

**Logged**: $timestamp
**Tenant ID**: $tenantId
**Distribution ID**: $distributionValue
**Attestation Status**: $attestationStatusValue
**Revocation SLO Status**: $sloStatusValue
**Revocation SLO Age Hours**: $sloAgeValue
**Baseline Window**: $attestationBaselineWindow
**Baseline Samples**: $attestationBaselineSamples
**Baseline Alert Count**: $attestationBaselineAlertCount
**Anomaly Percent**: $attestationAnomalyPercent%
**Weighted Anomaly Percent**: $weightedAnomalyPercent%
**Anomaly Threshold Percent**: $attestationAnomalyThresholdPercent%
**Calibrated Threshold Percent**: $calibratedAttestationThresholdPercent%
**Tenant Criticality Tier**: $tenantCriticalityTier
**Adaptive Threshold Source**: $adaptiveThresholdSource
**Time-Decay Half-Life Samples**: $timeDecayHalfLifeEffective
**Time-Decay Source**: $timeDecaySource
**Time-Decay Weighting Enabled**: $enableTimeDecayWeighting
**Recurrence Ratio Percent**: $recurrenceRatioPercent%
**Recurrence Tuned Half-Life Samples**: $recurrenceTunedHalfLife
**Recurrence Auto-Tuning Source**: $recurrenceTuningSource
**Recurrence Auto-Tuning Enabled**: $enableRecurrenceAutoTuning
**Seasonal Phase**: $seasonalPhase
**Seasonal Multiplier**: $seasonalMultiplier
**Seasonal Tuned Half-Life Samples**: $seasonalTunedHalfLife
**Seasonal Decomposition Source**: $seasonalDecompositionSource
**Seasonal Decomposition Enabled**: $enableSeasonalRecurrenceDecomposition
**Seasonal Overlay Name**: $seasonalOverlayName
**Seasonal Overlay Multiplier**: $seasonalOverlayMultiplier
**Seasonal Overlay Source**: $seasonalOverlaySource
**Seasonal Overlay Tuning Enabled**: $enableSeasonalOverlayTuning
**Causal Cluster Key**: $causalClusterKey
**Causal Cluster Sample Count**: $causalClusterSampleCount
**Causal Cluster Fail Count**: $causalClusterFailCount
**Causal Cluster Fail Percent**: $causalClusterFailPercent%
**Suggested Overlay Candidate**: $causalClusterSuggestedOverlay
**Suggested Overlay Multiplier**: $causalClusterSuggestedMultiplier
**Causal Clustering Source**: $causalClusterSource
**Causal Clustering Enabled**: $enableCausalClustering
**Causal Confidence Score**: $causalConfidenceScore
**Causal Confidence Threshold**: $causalConfidenceThreshold
**Causal Confidence Source**: $causalConfidenceSource
**Graph Cohort Window**: $graphCohortWindow
**Graph Temporal Decay Rate**: $graphTemporalDecayRate
**Graph Temporal Source**: $graphTemporalSource
**Edge Persistence Percent**: $edgePersistencePercent%
**Edge Persistence Score**: $edgePersistenceScore
**Graph Edge Persistence Enabled**: $enableGraphEdgePersistence
**Causal Graph Node**: $causalGraphNode
**Causal Graph Edge**: $causalGraphEdge
**Anomaly Gate Enabled**: $requireAttestationBaselineGate

---
"@
$explainabilityHistoryRecord =
    "$timestamp,$tenantId,$rolloutPhase,$dependency,$(if ($failureMode) { $failureMode } else { 'unspecified' }),$explainSampleContrib,$explainFailContrib,$explainContextContrib,$explainPersistContrib,$causalConfidenceScore,$causalClusterSuggestedOverlay,$(if ($causalClusterSuggestedOverlay -eq 'none') { 'below-threshold-or-no-signal' } else { 'qualified' })"
$explainerDiffEntry = ''
$driftRootCauseEntry = ''
if ($enableExplainerDiffing) {
    $effectiveFailureModeForDiff = if ($failureMode) { $failureMode } else { 'unspecified' }
    $prevExplainRow = Import-Csv -LiteralPath $explainabilityHistoryFile |
        Where-Object {
            $_.tenant_id -eq $tenantId -and
            $_.rollout_phase -eq $rolloutPhase -and
            $_.dependency -eq $dependency -and
            $_.failure_mode -eq $effectiveFailureModeForDiff
        } |
        Select-Object -Last 1

    if ($prevExplainRow) {
        [int]$prevSample = 0; [int]::TryParse([string]$prevExplainRow.sample_contrib, [ref]$prevSample) | Out-Null
        [int]$prevFail = 0; [int]::TryParse([string]$prevExplainRow.fail_contrib, [ref]$prevFail) | Out-Null
        [int]$prevContext = 0; [int]::TryParse([string]$prevExplainRow.context_contrib, [ref]$prevContext) | Out-Null
        [int]$prevPersist = 0; [int]::TryParse([string]$prevExplainRow.persistence_contrib, [ref]$prevPersist) | Out-Null
        [int]$prevConfidence = 0; [int]::TryParse([string]$prevExplainRow.confidence_score, [ref]$prevConfidence) | Out-Null

        $deltaSample = $explainSampleContrib - $prevSample
        $deltaFail = $explainFailContrib - $prevFail
        $deltaContext = $explainContextContrib - $prevContext
        $deltaPersist = $explainPersistContrib - $prevPersist
        $deltaConfidence = $causalConfidenceScore - $prevConfidence
        $driftState = if ([Math]::Abs($deltaConfidence) -ge $explainerDriftThreshold) { 'drift-detected' } else { 'stable' }

        $explainerDiffEntry = @"
## [XDIFF-$entryId] explainer_diff_consecutive_cohorts

**Logged**: $timestamp
**Tenant ID**: $tenantId
**Cluster Key**: $causalClusterKey
**Previous Timestamp**: $($prevExplainRow.timestamp)
**Current Confidence Score**: $causalConfidenceScore
**Previous Confidence Score**: $prevConfidence
**Confidence Delta**: $deltaConfidence
**Sample Contribution Delta**: $deltaSample
**Fail Contribution Delta**: $deltaFail
**Context Contribution Delta**: $deltaContext
**Persistence Contribution Delta**: $deltaPersist
**Drift Threshold**: $explainerDriftThreshold
**Drift State**: $driftState

---
"@

        if ($driftState -eq 'drift-detected' -and $enableDriftRootCauseSynthesis) {
            $factorDeltas = @{
                sample = $deltaSample
                fail = $deltaFail
                context = $deltaContext
                persistence = $deltaPersist
            }
            $dominantFactor = 'sample'
            $dominantDelta = $deltaSample
            $maxAbs = [Math]::Abs($deltaSample)
            foreach ($k in $factorDeltas.Keys) {
                $candidate = [int]$factorDeltas[$k]
                if ([Math]::Abs($candidate) -gt $maxAbs) {
                    $maxAbs = [Math]::Abs($candidate)
                    $dominantFactor = $k
                    $dominantDelta = $candidate
                }
            }

            $factorReason = switch ($dominantFactor) {
                'sample' { 'Cohort sample-confidence changed most, indicating evidence volume or recency changed between cohorts.' }
                'fail' { 'Fail-impact contribution shifted most, indicating failure prevalence changed between consecutive cohorts.' }
                'context' { 'Context contribution shifted most, indicating policy-context inputs changed between cohorts.' }
                'persistence' { 'Persistence contribution shifted most, indicating temporal persistence behavior changed between cohorts.' }
                default { 'Contribution shifts indicate mixed-factor drift across cohorts.' }
            }

            $confidenceBoundsStatement = 'Confidence bounds disabled.'
            if ($enableProbabilisticConfidenceBounds) {
                $effectiveN = [Math]::Max(1, [int]$causalClusterSampleCount)
                $p = [Math]::Max(0.0, [Math]::Min(1.0, ([double]$causalClusterFailPercent / 100.0)))
                $margin = $confidenceBoundZScore * [Math]::Sqrt(($p * (1 - $p)) / $effectiveN)
                $low = [Math]::Max(0.0, $p - $margin)
                $high = [Math]::Min(1.0, $p + $margin)
                $confidenceLevel = if ($confidenceBoundZScore -ge 2.57) { '99%' } elseif ($confidenceBoundZScore -ge 1.96) { '95%' } elseif ($confidenceBoundZScore -ge 1.64) { '90%' } else { 'custom' }
                $confidenceBoundsStatement = "Approximate $confidenceLevel confidence interval for fail-impact signal is [$([Math]::Round($low * 100, 2))%, $([Math]::Round($high * 100, 2))%] using z=$confidenceBoundZScore and n=$effectiveN."

                if ($enableBayesianPosteriorConfidence) {
                    $bayesFailCount = [Math]::Max(0, [int]$causalClusterFailCount)
                    $bayesTotalCount = [Math]::Max(0, [int]$causalClusterSampleCount)
                    $bayesNonFail = [Math]::Max(0, $bayesTotalCount - $bayesFailCount)
                    $posteriorAlpha = [double]$bayesPriorAlpha + $bayesFailCount
                    $posteriorBeta = [double]$bayesPriorBeta + $bayesNonFail
                    $posteriorMean = if (($posteriorAlpha + $posteriorBeta) -gt 0) { $posteriorAlpha / ($posteriorAlpha + $posteriorBeta) } else { 0.0 }
                    $posteriorVarDenom = ($posteriorAlpha + $posteriorBeta) * ($posteriorAlpha + $posteriorBeta) * ($posteriorAlpha + $posteriorBeta + 1)
                    $posteriorVar = if ($posteriorVarDenom -gt 0) { ($posteriorAlpha * $posteriorBeta) / $posteriorVarDenom } else { 0.0 }
                    $posteriorStd = if ($posteriorVar -gt 0) { [Math]::Sqrt($posteriorVar) } else { 0.0 }
                    $posteriorMargin = $confidenceBoundZScore * $posteriorStd
                    $posteriorLow = [Math]::Max(0.0, $posteriorMean - $posteriorMargin)
                    $posteriorHigh = [Math]::Min(1.0, $posteriorMean + $posteriorMargin)
                    $confidenceBoundsStatement = "$confidenceBoundsStatement Bayesian posterior mean is $([Math]::Round($posteriorMean * 100, 2))% with approximate interval [$([Math]::Round($posteriorLow * 100, 2))%, $([Math]::Round($posteriorHigh * 100, 2))%] using prior alpha=$bayesPriorAlpha, beta=$bayesPriorBeta."
                }
            }

            $driftRootCauseEntry = @"
## [RCAUSE-$entryId] drift_root_cause_narrative

**Logged**: $timestamp
**Tenant ID**: $tenantId
**Cluster Key**: $causalClusterKey
**Drift State**: $driftState
**Confidence Delta**: $deltaConfidence
**Dominant Factor**: $dominantFactor
**Dominant Factor Delta**: $dominantDelta

### Narrative
$factorReason Confidence changed by $deltaConfidence points versus the prior cohort and exceeded the drift threshold of $explainerDriftThreshold. $confidenceBoundsStatement

---
"@
        }
    }
}
$graphExplainabilityEntry = @"
## [XPL-$entryId] cohort_aware_graph_explainability

**Logged**: $timestamp
**Tenant ID**: $tenantId
**Cluster Key**: $causalClusterKey
**Cohort Window**: $graphCohortWindow
**Temporal Decay Rate**: $graphTemporalDecayRate
**Temporal Source**: $graphTemporalSource
**Sample Confidence Percent**: $sampleConfidence%
**Fail Impact Percent**: $causalClusterFailPercent%
**Context Score**: $contextScore
**Edge Persistence Percent**: $edgePersistencePercent%
**Sample Contribution**: $explainSampleContrib
**Fail Contribution**: $explainFailContrib
**Context Contribution**: $explainContextContrib
**Persistence Contribution**: $explainPersistContrib
**Confidence Score**: $causalConfidenceScore
**Confidence Threshold**: $causalConfidenceThreshold
**Recommended Overlay**: $causalClusterSuggestedOverlay
**Recommendation Decision**: $(if ($causalClusterSuggestedOverlay -eq 'none') { 'below-threshold-or-no-signal' } else { 'qualified' })

---
"@

        if ($dryRun) {
            Write-Host "[DRY-RUN] Would append tenant attestation dashboard entry to ${attestationDashboardFile}:"
            Write-Host $dashboardEntry
        } else {
            if (-not (Test-Path -LiteralPath $attestationDashboardFile)) {
                Set-Content -LiteralPath $attestationDashboardFile -Value '# Tenant Trust-Policy Attestation Dashboard'
                Add-Content -LiteralPath $attestationDashboardFile -Value ''
            }
            Add-Content -LiteralPath $attestationDashboardFile -Value $dashboardEntry
        }

        if ($enableCrossTenantHeatmap) {
            $crossRows = Import-Csv -LiteralPath $attestationTrendHistoryFile |
                Select-Object -Last $attestationBaselineWindow
            $tenantGroups = $crossRows | Group-Object tenant_id

            $heatmapLines = @()
            foreach ($group in $tenantGroups) {
                $tenantSamples = $group.Count
                $tenantAlerts = @($group.Group | Where-Object {
                        $_.attestation_status -ne 'verified' -or
                        $_.revocation_slo_status -eq 'warning'
                    }).Count

                $tenantPercent = 0
                if ($tenantSamples -gt 0) {
                    $tenantPercent = [int](($tenantAlerts * 100) / $tenantSamples)
                }

                $tenantSeverity = 'low'
                $tenantRoute = 'monitor'
                if ($tenantPercent -ge 80) {
                    $tenantSeverity = 'critical'
                    $tenantRoute = 'auto-remediation'
                } elseif ($tenantPercent -ge 60) {
                    $tenantSeverity = 'high'
                    $tenantRoute = 'incident-review'
                } elseif ($tenantPercent -ge 35) {
                    $tenantSeverity = 'medium'
                    $tenantRoute = 'owner-review'
                }

                $heatmapLines +=
                    "- tenant: $($group.Name), anomaly-percent: $tenantPercent, severity: $tenantSeverity, route: $tenantRoute"
            }

            if ($heatmapLines.Count -eq 0) {
                $heatmapLines += '- tenant: none, anomaly-percent: 0, severity: low'
            }

            $heatmapEntry = @"
## [HEATMAP-$entryId] cross_tenant_attestation_anomaly

**Logged**: $timestamp
**Baseline Window**: $attestationBaselineWindow
**Anomaly Threshold Percent**: $attestationAnomalyThresholdPercent

### Tenant Heatmap
$($heatmapLines -join "`n")

---
"@

            $routingRecommendation = if ($attestationAnomalyPercent -ge 80) {
                'auto-remediation'
            } elseif ($attestationAnomalyPercent -ge 60) {
                'incident-review'
            } elseif ($attestationAnomalyPercent -ge 35) {
                'owner-review'
            } else {
                'monitor'
            }

            $routingEntry = @"
## [ROUTE-$entryId] cross_tenant_auto_remediation_routing

**Logged**: $timestamp
**Source Simulation**: $simulationId
**Tenant ID**: $tenantId
**Anomaly Percent**: $attestationAnomalyPercent%
**Routing Recommendation**: $routingRecommendation
**Target Endpoint**: gateway.runtime.governance.remediationPlan
**Routing Policy**: tenant anomaly severity bands

---
"@

            if ($dryRun) {
                Write-Host "[DRY-RUN] Would append cross-tenant heatmap entry to ${crossTenantHeatmapFile}:"
                Write-Host $heatmapEntry
                Write-Host "[DRY-RUN] Would append auto-remediation routing entry to ${autoRemediationRoutingFile}:"
                Write-Host $routingEntry
            } else {
                if (-not (Test-Path -LiteralPath $crossTenantHeatmapFile)) {
                    Set-Content -LiteralPath $crossTenantHeatmapFile -Value '# Cross-Tenant Attestation Anomaly Heatmap'
                    Add-Content -LiteralPath $crossTenantHeatmapFile -Value ''
                }
                if (-not (Test-Path -LiteralPath $autoRemediationRoutingFile)) {
                    Set-Content -LiteralPath $autoRemediationRoutingFile -Value '# Cross-Tenant Auto-Remediation Recommendation Routing'
                    Add-Content -LiteralPath $autoRemediationRoutingFile -Value ''
                }
                Add-Content -LiteralPath $crossTenantHeatmapFile -Value $heatmapEntry
                Add-Content -LiteralPath $autoRemediationRoutingFile -Value $routingEntry
            }
        }
    }
}

$profileRows = Import-Csv -LiteralPath $profileWeightsFile
if (-not $profileRows) {
    throw "Malformed profile weights file (empty or invalid CSV): $profileWeightsFile"
}

$profileRow = $profileRows |
    Where-Object { $_.profile -eq $policyProfile } |
    Select-Object -First 1

if (-not $profileRow) {
    throw "Missing required policy profile '$policyProfile' in: $profileWeightsFile"
}

$profileSchemaVersion = ''
$schemaColumnPresent =
    ($profileRow.PSObject.Properties.Name -contains 'schema_version')
if ($schemaColumnPresent) {
    $profileSchemaVersion = [string]$profileRow.schema_version
}

if (-not [string]::IsNullOrWhiteSpace($strictSchemaVersion)) {
    if (-not $schemaColumnPresent) {
        throw "Malformed profile weights file: schema_version column is required for --strict-schema-version in $profileWeightsFile"
    }

    if ([string]::IsNullOrWhiteSpace($profileSchemaVersion)) {
        throw "Malformed profile '$policyProfile': schema_version value is required for --strict-schema-version in $profileWeightsFile"
    }

    if ($profileSchemaVersion -ne $strictSchemaVersion) {
        throw "Schema version mismatch for profile '$policyProfile': expected '$strictSchemaVersion' but found '$profileSchemaVersion' in $profileWeightsFile"
    }
}

$numericFields = @(
    'fail_base',
    'pass_base',
    'phase_r1',
    'phase_r2',
    'phase_r3',
    'phase_r4',
    'dependency_registry',
    'dependency_authority',
    'trend_fail_divisor',
    'trend_pass_divisor'
)

$parsed = @{}
foreach ($field in $numericFields) {
    [int]$value = 0
    if (-not [int]::TryParse([string]$profileRow.$field, [ref]$value)) {
        throw "Malformed profile '$policyProfile': field '$field' must be numeric in $profileWeightsFile"
    }
    $parsed[$field] = $value
}

if ($parsed['trend_fail_divisor'] -le 0 -or
    $parsed['trend_pass_divisor'] -le 0) {
    throw "Malformed profile '$policyProfile': trend divisors must be > 0 in $profileWeightsFile"
}

$failBaseScore = $parsed['fail_base']
$passBaseScore = $parsed['pass_base']
$phaseR1Weight = $parsed['phase_r1']
$phaseR2Weight = $parsed['phase_r2']
$phaseR3Weight = $parsed['phase_r3']
$phaseR4Weight = $parsed['phase_r4']
$dependencyRegistryWeight = $parsed['dependency_registry']
$dependencyAuthorityWeight = $parsed['dependency_authority']
$trendFailDivisor = $parsed['trend_fail_divisor']
$trendPassDivisor = $parsed['trend_pass_divisor']

$phaseWeight = switch ($rolloutPhase) {
    'r1' { $phaseR1Weight }
    'r2' { $phaseR2Weight }
    'r3' { $phaseR3Weight }
    'r4' { $phaseR4Weight }
}

$dependencyWeight = if ($dependency -eq 'authority') {
    $dependencyAuthorityWeight
} else {
    $dependencyRegistryWeight
}

$recommendationScore = if ($result -eq 'fail') {
    $failBaseScore + $phaseWeight + $dependencyWeight +
        [int]($trendFailRate / $trendFailDivisor)
} else {
    $passBaseScore + [int]($phaseWeight / 2) +
        [int]($dependencyWeight / 2) +
        [int]($trendFailRate / $trendPassDivisor)
}

if ($recommendationScore -gt 100) {
    $recommendationScore = 100
}

$severity = 'low'
if ($recommendationScore -ge 80) {
    $severity = 'critical'
} elseif ($recommendationScore -ge 60) {
    $severity = 'high'
} elseif ($recommendationScore -ge 35) {
    $severity = 'medium'
}

$category = 'best_practice'
$status = 'promoted'
if ($result -eq 'fail') {
    $category = 'knowledge_gap'
    $status = 'pending'
}

$baseRecommendation = ''
$controlKeys = ''
if ($dependency -eq 'registry') {
    $controlKeys =
        'hooks.engine.policyRegistrySyncMode, hooks.engine.registryOutageSimulationEnabled, hooks.engine.registryFailoverRunbookId'
    if ($result -eq 'pass') {
        $baseRecommendation =
            'Keep registry failover controls enabled and tighten sync health alert thresholds for promotion gates.'
    } else {
        $baseRecommendation =
            'Enable registry outage simulation and enforce fallback bundle pin validation before promotion.'
    }
} else {
    $controlKeys =
        'hooks.engine.attestationRevocationMode, hooks.engine.authorityOutageSimulationEnabled, hooks.engine.authorityFailoverRunbookId'
    if ($result -eq 'pass') {
        $baseRecommendation =
            'Retain authority failover endpoint coverage and reduce tolerated verification latency in rollout policy.'
    } else {
        $baseRecommendation =
            'Enforce strict authority trust-chain validation and block publication until failover verification recovers.'
    }
}

$phaseGuidance =
    "Phase $rolloutPhase score band is $severity (score: $recommendationScore)."
if ($result -eq 'fail' -and ($rolloutPhase -eq 'r3' -or $rolloutPhase -eq 'r4')) {
    $phaseGuidance =
        "$phaseGuidance Promotion should be blocked until remediation evidence is verified."
} elseif ($result -eq 'pass' -and $severity -eq 'low') {
    $phaseGuidance =
        "$phaseGuidance Eligible for next phase with routine monitoring."
}

$recommendation = "$baseRecommendation $phaseGuidance"

$learningEntry = @"
## [LRN-$entryId] $category

**Logged**: $timestamp
**Priority**: high
**Status**: $status
**Promoted**: AGENTS.md
**Area**: infra

### Summary
Outage simulation $simulationId reported $result for $dependency dependency in tenant $tenantId ($rolloutPhase)

### Details
- Simulation ID: $simulationId
- Tenant ID: $tenantId
- Rollout Phase: $rolloutPhase
- Policy Profile: $policyProfile
- Policy Profile Schema Version: $(if ($profileSchemaVersion) { $profileSchemaVersion } else { 'not-declared' })
- Dependency: $dependency
- Result: $result
- Failure Mode: $(if ($failureMode) { $failureMode } else { 'not-provided' })
- Drill Window: $(if ($drillWindow) { $drillWindow } else { 'not-provided' })
- Evidence Path: $evidencePath
- Automated Failover Triggered: $(if ($failoverTriggered) { $failoverTriggered } else { 'not-provided' })
- Automated Failback Completed: $(if ($failbackCompleted) { $failbackCompleted } else { 'not-provided' })
- Trend Window Size: $trendWindowSize
- Trend Segment: tenant=$tenantId, dependency=$dependency
- Trend Sample Count: $trendTotal
- Trend Fail Count: $trendFailCount
- Trend Pass Count: $trendPassCount
- Trend Fail Rate: $trendFailRate%

### Suggested Action
$recommendation

### Metadata
- Source: outage_simulation
- Related Files: references/enterprise-policy-attestation-publication-template.md, references/hook-rollout-policy-template.md
- Tags: outage-simulation, failover, policy-tuning, governance, tenant-scoped, phase-aware, profile-weighted

---
"@

$policyEntry = @"
## [REC-$entryId] policy_tuning

**Logged**: $timestamp
**Source Simulation**: $simulationId
**Tenant ID**: $tenantId
**Rollout Phase**: $rolloutPhase
**Policy Profile**: $policyProfile
**Dependency**: $dependency
**Outcome**: $result
**Status**: suggested

### Recommendation
$recommendation

### Scoring
- Recommendation Score: $recommendationScore
- Severity: $severity
- Trend Window Size: $trendWindowSize
- Trend Segment: tenant=$tenantId, dependency=$dependency
- Trend Sample Count: $trendTotal
- Trend Fail Count: $trendFailCount
- Trend Pass Count: $trendPassCount
- Trend Fail Rate: $trendFailRate%
- Weight Source: $profileWeightsFile
- Manifest Source: $profileManifestFile
- Manifest Version: $(if ($manifestVersion) { $manifestVersion } else { 'not-verified' })
- Signed By: $(if ($manifestSignedBy) { $manifestSignedBy } else { 'not-verified' })
- Signed At: $(if ($manifestSignedAt) { $manifestSignedAt } else { 'not-verified' })
- Key Id: $(if ($manifestKeyId) { $manifestKeyId } else { 'not-verified' })
- Manifest Signature: $(if ($manifestSignature) { $manifestSignature } else { 'not-verified' })
- Manifest Signature Scheme: $(if ($manifestSignatureScheme) { $manifestSignatureScheme } else { 'not-verified' })
- Manifest Signature File: $(if ($manifestSignatureFile) { $manifestSignatureFile } else { 'not-verified' })
- Manifest Certificate File: $(if ($manifestCertificateFile) { $manifestCertificateFile } else { 'not-verified' })
- Weight Schema Version: $(if ($profileSchemaVersion) { $profileSchemaVersion } else { 'not-declared' })
- Strict Schema Gate: $(if ($strictSchemaVersion) { $strictSchemaVersion } else { 'disabled' })
- Strict Manifest Gate: $requireSignedManifest
- Signature Verification Mode: $signatureVerificationMode
- Trust Policy Source: $trustPolicyFile
- Trust Policy Distribution Id: $(if ($trustPolicyDistributionId) { $trustPolicyDistributionId } else { 'not-verified' })
- Trust Policy Distributed At: $(if ($trustPolicyDistributedAt) { $trustPolicyDistributedAt } else { 'not-verified' })
- Trust Policy Federation Scope: $(if ($trustPolicyFederationScope) { $trustPolicyFederationScope } else { 'not-verified' })
- Trust Policy Active Key Count: $trustPolicyActiveKeyCount
- Trust Policy Revoked Key Count: $trustPolicyRevokedKeyCount
- Trust Policy Gate: $requireTrustPolicy
- Trust Policy Attestation Source: $trustPolicyAttestationFile
- Trust Policy Attestation Distribution Id: $(if ($attestationDistributionId) { $attestationDistributionId } else { 'not-verified' })
- Trust Policy Attestation Published At: $(if ($attestationPublishedAt) { $attestationPublishedAt } else { 'not-verified' })
- Trust Policy Attestation Status: $(if ($attestationStatus) { $attestationStatus } else { 'not-verified' })
- Trust Policy Attestation Signer: $(if ($attestationSigner) { $attestationSigner } else { 'not-verified' })
- Trust Policy Attestation Gate: $requireTrustPolicyAttestation
- Revocation List Source: $revocationListFile
- Revocation Check Gate: $requireRevocationCheck
- Revocation SLO Source: $revocationSloFile
- Revocation SLO Max Hours: $(if ($revocationSloMaxHours) { $revocationSloMaxHours } else { 'not-verified' })
- Revocation SLO Last Check At: $(if ($revocationSloLastCheckAt) { $revocationSloLastCheckAt } else { 'not-verified' })
- Revocation SLO Status: $(if ($revocationSloStatus) { $revocationSloStatus } else { 'not-verified' })
- Revocation SLO Gate: $requireRevocationSlo
- Attestation Dashboard Source: $attestationDashboardFile
- Attestation Trend History Source: $attestationTrendHistoryFile
- Attestation Baseline Window: $attestationBaselineWindow
- Attestation Anomaly Threshold Percent: $attestationAnomalyThresholdPercent
- Attestation Baseline Samples: $attestationBaselineSamples
- Attestation Baseline Alert Count: $attestationBaselineAlertCount
- Attestation Anomaly Percent: $attestationAnomalyPercent%
- Attestation Weighted Anomaly Percent: $weightedAnomalyPercent%
- Attestation Calibrated Threshold Percent: $calibratedAttestationThresholdPercent
- Tenant Criticality Tier: $tenantCriticalityTier
- Adaptive Threshold Source: $adaptiveThresholdSource
- Adaptive Threshold Calibration Enabled: $enableAdaptiveThresholdCalibration
- Time-Decay Half-Life Samples: $timeDecayHalfLifeEffective
- Time-Decay Source: $timeDecaySource
- Time-Decay Weighting Enabled: $enableTimeDecayWeighting
- Recurrence Ratio Percent: $recurrenceRatioPercent%
- Recurrence Tuned Half-Life Samples: $recurrenceTunedHalfLife
- Recurrence Auto-Tuning Source: $recurrenceTuningSource
- Recurrence Auto-Tuning Enabled: $enableRecurrenceAutoTuning
- Seasonal Phase: $seasonalPhase
- Seasonal Multiplier: $seasonalMultiplier
- Seasonal Tuned Half-Life Samples: $seasonalTunedHalfLife
- Seasonal Decomposition Source: $seasonalDecompositionSource
- Seasonal Decomposition Enabled: $enableSeasonalRecurrenceDecomposition
- Seasonal Overlay Name: $seasonalOverlayName
- Seasonal Overlay Multiplier: $seasonalOverlayMultiplier
- Seasonal Overlay Source: $seasonalOverlaySource
- Seasonal Overlay Tuning Enabled: $enableSeasonalOverlayTuning
- Causal Cluster Key: $causalClusterKey
- Causal Cluster Sample Count: $causalClusterSampleCount
- Causal Cluster Fail Count: $causalClusterFailCount
- Causal Cluster Fail Percent: $causalClusterFailPercent%
- Suggested Overlay Candidate: $causalClusterSuggestedOverlay
- Suggested Overlay Multiplier: $causalClusterSuggestedMultiplier
- Causal Clustering Source: $causalClusterSource
- Causal Clustering Enabled: $enableCausalClustering
- Causal Confidence Score: $causalConfidenceScore
- Causal Confidence Threshold: $causalConfidenceThreshold
- Causal Confidence Source: $causalConfidenceSource
- Graph Cohort Window: $graphCohortWindow
- Graph Temporal Decay Rate: $graphTemporalDecayRate
- Graph Temporal Source: $graphTemporalSource
- Edge Persistence Percent: $edgePersistencePercent%
- Edge Persistence Score: $edgePersistenceScore
- Graph Edge Persistence Enabled: $enableGraphEdgePersistence
- Causal Graph Node: $causalGraphNode
- Causal Graph Edge: $causalGraphEdge
- Attestation Baseline Gate: $requireAttestationBaselineGate
- Cross-Tenant Heatmap Source: $crossTenantHeatmapFile
- Auto-Remediation Routing Source: $autoRemediationRoutingFile
- Cross-Tenant Heatmap Enabled: $enableCrossTenantHeatmap
- Weight Inputs:
  - fail-base=$failBaseScore
  - pass-base=$passBaseScore
  - phase-r1=$phaseR1Weight
  - phase-r2=$phaseR2Weight
  - phase-r3=$phaseR3Weight
  - phase-r4=$phaseR4Weight
  - dependency-registry=$dependencyRegistryWeight
  - dependency-authority=$dependencyAuthorityWeight
  - trend-fail-divisor=$trendFailDivisor
  - trend-pass-divisor=$trendPassDivisor

### Target Controls
- $controlKeys

### Evidence
- Drill Report: $evidencePath
- Failure Mode: $(if ($failureMode) { $failureMode } else { 'not-provided' })
- Drill Window: $(if ($drillWindow) { $drillWindow } else { 'not-provided' })
- Automated Failover Triggered: $(if ($failoverTriggered) { $failoverTriggered } else { 'not-provided' })
- Automated Failback Completed: $(if ($failbackCompleted) { $failbackCompleted } else { 'not-provided' })

### Notes
$(if ($notes) { $notes } else { 'none' })

---
"@
$causalGraphEntry = @"
## [GRAPH-$entryId] confidence_weighted_causal_graph

**Logged**: $timestamp
**Tenant ID**: $tenantId
**Node**: $causalGraphNode
**Edge**: $causalGraphEdge
**Confidence Score**: $causalConfidenceScore
**Confidence Threshold**: $causalConfidenceThreshold
**Confidence Source**: $causalConfidenceSource
**Graph Cohort Window**: $graphCohortWindow
**Graph Temporal Decay Rate**: $graphTemporalDecayRate
**Graph Temporal Source**: $graphTemporalSource
**Edge Persistence Percent**: $edgePersistencePercent%
**Edge Persistence Score**: $edgePersistenceScore
**Graph Edge Persistence Enabled**: $enableGraphEdgePersistence

---
"@

$trendRecord =
    "$timestamp,$tenantId,$rolloutPhase,$dependency,$result,$simulationId"
$causalHistoryRecord =
    "$timestamp,$tenantId,$rolloutPhase,$dependency,$(if ($failureMode) { $failureMode } else { 'unspecified' }),$result,$simulationId"
$overlayCandidateEntry = @"
## [OVR-$entryId] overlay_candidate

**Logged**: $timestamp
**Tenant ID**: $tenantId
**Cluster Key**: $causalClusterKey
**Sample Count**: $causalClusterSampleCount
**Fail Count**: $causalClusterFailCount
**Fail Percent**: $causalClusterFailPercent%
**Suggested Overlay Name**: $causalClusterSuggestedOverlay
**Suggested Overlay Multiplier**: $causalClusterSuggestedMultiplier
**Source**: $causalClusterSource

---
"@

if ($dryRun) {
    Write-Host "[DRY-RUN] Would append to ${learningsFile}:"
    Write-Host $learningEntry
    Write-Host "[DRY-RUN] Would append to ${policyTuningFile}:"
    Write-Host $policyEntry
    Write-Host "[DRY-RUN] Would append trend record to ${trendHistoryFile}:"
    Write-Host $trendRecord
    Write-Host "[DRY-RUN] Would append causal history record to ${causalHistoryFile}:"
    Write-Host $causalHistoryRecord
    Write-Host "[DRY-RUN] Would append overlay candidate suggestion to ${overlayCandidateFile}:"
    Write-Host $overlayCandidateEntry
    Write-Host "[DRY-RUN] Would append causal confidence graph entry to ${causalGraphFile}:"
    Write-Host $causalGraphEntry
    if ($enableGraphExplainabilityTraces) {
        Write-Host "[DRY-RUN] Would append cohort explainability trace entry to ${graphExplainabilityFile}:"
        Write-Host $graphExplainabilityEntry
        Write-Host "[DRY-RUN] Would append explainability history record to ${explainabilityHistoryFile}:"
        Write-Host $explainabilityHistoryRecord
        if (-not [string]::IsNullOrWhiteSpace($explainerDiffEntry)) {
            Write-Host "[DRY-RUN] Would append explainer diff entry to ${graphExplainabilityFile}:"
            Write-Host $explainerDiffEntry
            if (-not [string]::IsNullOrWhiteSpace($driftRootCauseEntry)) {
                Write-Host "[DRY-RUN] Would append drift root-cause narrative entry to ${driftRootCauseFile}:"
                Write-Host $driftRootCauseEntry
            }
        }
    }
    exit 0
}

if (-not (Test-Path -LiteralPath $learningsFile)) {
    throw "Missing learnings file: $learningsFile"
}

if (-not (Test-Path -LiteralPath $policyTuningFile)) {
    throw "Missing policy tuning file: $policyTuningFile"
}

Add-Content -LiteralPath $learningsFile -Value $learningEntry
Add-Content -LiteralPath $policyTuningFile -Value $policyEntry
Add-Content -LiteralPath $trendHistoryFile -Value $trendRecord
Add-Content -LiteralPath $causalHistoryFile -Value $causalHistoryRecord
if (-not (Test-Path -LiteralPath $overlayCandidateFile)) {
    Set-Content -LiteralPath $overlayCandidateFile -Value '# Seasonal Overlay Candidate Suggestions'
    Add-Content -LiteralPath $overlayCandidateFile -Value ''
}
Add-Content -LiteralPath $overlayCandidateFile -Value $overlayCandidateEntry
if (-not (Test-Path -LiteralPath $causalGraphFile)) {
    Set-Content -LiteralPath $causalGraphFile -Value '# Confidence-Weighted Causal Graph'
    Add-Content -LiteralPath $causalGraphFile -Value ''
}
Add-Content -LiteralPath $causalGraphFile -Value $causalGraphEntry
if ($enableGraphExplainabilityTraces) {
    if (-not (Test-Path -LiteralPath $graphExplainabilityFile)) {
        Set-Content -LiteralPath $graphExplainabilityFile -Value '# Cohort-Aware Causal Graph Explainability Traces'
        Add-Content -LiteralPath $graphExplainabilityFile -Value ''
    }
    Add-Content -LiteralPath $graphExplainabilityFile -Value $graphExplainabilityEntry
    Add-Content -LiteralPath $explainabilityHistoryFile -Value $explainabilityHistoryRecord
    if (-not [string]::IsNullOrWhiteSpace($explainerDiffEntry)) {
        Add-Content -LiteralPath $graphExplainabilityFile -Value $explainerDiffEntry
        if (-not [string]::IsNullOrWhiteSpace($driftRootCauseEntry)) {
            if (-not (Test-Path -LiteralPath $driftRootCauseFile)) {
                Set-Content -LiteralPath $driftRootCauseFile -Value '# Causal Drift Root-Cause Narratives'
                Add-Content -LiteralPath $driftRootCauseFile -Value ''
            }
            Add-Content -LiteralPath $driftRootCauseFile -Value $driftRootCauseEntry
        }
    }
}

Write-Host "Appended outage simulation learning entry to $learningsFile"
Write-Host "Appended policy tuning recommendation to $policyTuningFile"
Write-Host "Appended tenant trend record to $trendHistoryFile"
if ($enableAttestationDashboard) {
    Write-Host "Updated tenant attestation trend history at $attestationTrendHistoryFile"
    Write-Host "Updated tenant attestation dashboard at $attestationDashboardFile"
    if ($enableCrossTenantHeatmap) {
        Write-Host "Updated cross-tenant anomaly heatmap at $crossTenantHeatmapFile"
        Write-Host "Updated auto-remediation routing at $autoRemediationRoutingFile"
    }
}
