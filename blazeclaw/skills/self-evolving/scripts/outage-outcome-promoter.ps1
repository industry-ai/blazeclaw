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

function Show-Usage {
@"
Usage: $(Split-Path -Leaf $PSCommandPath) --simulation-id <id> --tenant-id <tenant> --rollout-phase <r1|r2|r3|r4> --dependency <registry|authority> --result <pass|fail> --evidence-path <path> [options]

Required:
  --simulation-id       Outage simulation identifier (example: SIM-REG-001)
  --tenant-id           Tenant identifier for trend analysis
  --rollout-phase       Rollout phase (r1|r2|r3|r4)
  --dependency          Target dependency (registry|authority)
  --result              Simulation result (pass|fail)
  --evidence-path       Path to drill evidence artifact

Optional:
  --drill-window        Drill execution window
  --failure-mode        Failure mode exercised during drill
  --failover-triggered  Automated failover status (yes|no)
  --failback-completed  Automated failback status (yes|no)
  --trend-window-size   Number of recent tenant outcomes to analyze (default: 20)
  --notes               Additional context
  --dry-run             Print entries without writing files
  -h, --help            Show this help message
"@
}

$simulationId = ''
$tenantId = ''
$rolloutPhase = ''
$dependency = ''
$result = ''
$evidencePath = ''
$drillWindow = ''
$failureMode = ''
$failoverTriggered = ''
$failbackCompleted = ''
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
        Where-Object { $_.tenant_id -eq $tenantId } |
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

$phaseWeight = switch ($rolloutPhase) {
    'r1' { 5 }
    'r2' { 10 }
    'r3' { 15 }
    'r4' { 20 }
}

$dependencyWeight = if ($dependency -eq 'authority') { 12 } else { 8 }

$recommendationScore = if ($result -eq 'fail') {
    45 + $phaseWeight + $dependencyWeight + [int]($trendFailRate / 5)
} else {
    10 + [int]($phaseWeight / 2) +
        [int]($dependencyWeight / 2) + [int]($trendFailRate / 10)
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
- Dependency: $dependency
- Result: $result
- Failure Mode: $(if ($failureMode) { $failureMode } else { 'not-provided' })
- Drill Window: $(if ($drillWindow) { $drillWindow } else { 'not-provided' })
- Evidence Path: $evidencePath
- Automated Failover Triggered: $(if ($failoverTriggered) { $failoverTriggered } else { 'not-provided' })
- Automated Failback Completed: $(if ($failbackCompleted) { $failbackCompleted } else { 'not-provided' })
- Trend Window Size: $trendWindowSize
- Trend Sample Count: $trendTotal
- Trend Fail Count: $trendFailCount
- Trend Pass Count: $trendPassCount
- Trend Fail Rate: $trendFailRate%

### Suggested Action
$recommendation

### Metadata
- Source: outage_simulation
- Related Files: references/enterprise-policy-attestation-publication-template.md, references/hook-rollout-policy-template.md
- Tags: outage-simulation, failover, policy-tuning, governance, tenant-scoped, phase-aware

---
"@

$policyEntry = @"
## [REC-$entryId] policy_tuning

**Logged**: $timestamp
**Source Simulation**: $simulationId
**Tenant ID**: $tenantId
**Rollout Phase**: $rolloutPhase
**Dependency**: $dependency
**Outcome**: $result
**Status**: suggested

### Recommendation
$recommendation

### Scoring
- Recommendation Score: $recommendationScore
- Severity: $severity
- Trend Window Size: $trendWindowSize
- Trend Sample Count: $trendTotal
- Trend Fail Count: $trendFailCount
- Trend Pass Count: $trendPassCount
- Trend Fail Rate: $trendFailRate%

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
