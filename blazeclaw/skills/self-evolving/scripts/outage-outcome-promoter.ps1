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

if ([string]::IsNullOrWhiteSpace($policyProfile)) {
    throw '--policy-profile cannot be empty'
}

if ($signatureVerificationMode -notin @('none', 'kms', 'sigstore')) {
    throw '--signature-verification-mode must be one of: none, kms, sigstore'
}

if ($signatureVerificationMode -ne 'none') {
    $requireSignedManifest = $true
}

if (-not (Test-Path -LiteralPath $trendHistoryFile)) {
    Set-Content -LiteralPath $trendHistoryFile -NoNewline -Value
        'timestamp,tenant_id,rollout_phase,dependency,result,simulation_id'
    Add-Content -LiteralPath $trendHistoryFile -Value ''
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
        if ($manifestSignatureScheme -ne $signatureVerificationMode) {
            throw "Signature verification mode mismatch: requested '$signatureVerificationMode' but manifest declares '$manifestSignatureScheme'"
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

$trendRecord =
    "$timestamp,$tenantId,$rolloutPhase,$dependency,$result,$simulationId"

if ($dryRun) {
    Write-Host "[DRY-RUN] Would append to ${learningsFile}:"
    Write-Host $learningEntry
    Write-Host "[DRY-RUN] Would append to ${policyTuningFile}:"
    Write-Host $policyEntry
    Write-Host "[DRY-RUN] Would append trend record to ${trendHistoryFile}:"
    Write-Host $trendRecord
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

Write-Host "Appended outage simulation learning entry to $learningsFile"
Write-Host "Appended policy tuning recommendation to $policyTuningFile"
Write-Host "Appended tenant trend record to $trendHistoryFile"
