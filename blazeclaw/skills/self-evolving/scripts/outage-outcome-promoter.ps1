#!/usr/bin/env pwsh
# Outage Outcome Promoter (BlazeClaw) - PowerShell
# Captures outage simulation outcomes and emits policy tuning recommendations.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$learningsFile = './blazeclaw/skills/self-evolving/.learnings/LEARNINGS.md'
$policyTuningFile =
    './blazeclaw/skills/self-evolving/.learnings/POLICY_TUNING_RECOMMENDATIONS.md'

function Show-Usage {
@"
Usage: $(Split-Path -Leaf $PSCommandPath) --simulation-id <id> --dependency <registry|authority> --result <pass|fail> --evidence-path <path> [options]

Required:
  --simulation-id       Outage simulation identifier (example: SIM-REG-001)
  --dependency          Target dependency (registry|authority)
  --result              Simulation result (pass|fail)
  --evidence-path       Path to drill evidence artifact

Optional:
  --drill-window        Drill execution window
  --failure-mode        Failure mode exercised during drill
  --failover-triggered  Automated failover status (yes|no)
  --failback-completed  Automated failback status (yes|no)
  --notes               Additional context
  --dry-run             Print entries without writing files
  -h, --help            Show this help message
"@
}

$simulationId = ''
$dependency = ''
$result = ''
$evidencePath = ''
$drillWindow = ''
$failureMode = ''
$failoverTriggered = ''
$failbackCompleted = ''
$notes = ''
$dryRun = $false

for ($i = 0; $i -lt $args.Length; $i++) {
    $arg = [string]$args[$i]
    switch ($arg) {
        '--simulation-id' {
            $i++
            $simulationId = [string]$args[$i]
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
    [string]::IsNullOrWhiteSpace($dependency) -or
    [string]::IsNullOrWhiteSpace($result) -or
    [string]::IsNullOrWhiteSpace($evidencePath)) {
    throw 'Missing required arguments. Use --help for usage.'
}

if ($dependency -notin @('registry', 'authority')) {
    throw "--dependency must be one of: registry, authority"
}

if ($result -notin @('pass', 'fail')) {
    throw "--result must be one of: pass, fail"
}

$timestamp = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
$dateStamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd')
$sanitizedSimulationId = ($simulationId -replace '[^A-Za-z0-9]', '')
$entryId = "OUT-$dateStamp-$sanitizedSimulationId"

$category = 'best_practice'
$status = 'promoted'
if ($result -eq 'fail') {
    $category = 'knowledge_gap'
    $status = 'pending'
}

$recommendation = ''
$controlKeys = ''
if ($dependency -eq 'registry') {
    $controlKeys =
        'hooks.engine.policyRegistrySyncMode, hooks.engine.registryOutageSimulationEnabled, hooks.engine.registryFailoverRunbookId'
    if ($result -eq 'pass') {
        $recommendation =
            'Keep registry failover controls enabled and tighten sync health alert thresholds for promotion gates.'
    } else {
        $recommendation =
            'Enable registry outage simulation and enforce fallback bundle pin validation before promotion.'
    }
} else {
    $controlKeys =
        'hooks.engine.attestationRevocationMode, hooks.engine.authorityOutageSimulationEnabled, hooks.engine.authorityFailoverRunbookId'
    if ($result -eq 'pass') {
        $recommendation =
            'Retain authority failover endpoint coverage and reduce tolerated verification latency in rollout policy.'
    } else {
        $recommendation =
            'Enforce strict authority trust-chain validation and block publication until failover verification recovers.'
    }
}

$learningEntry = @"
## [LRN-$entryId] $category

**Logged**: $timestamp
**Priority**: high
**Status**: $status
**Promoted**: AGENTS.md
**Area**: infra

### Summary
Outage simulation $simulationId reported $result for $dependency dependency

### Details
- Simulation ID: $simulationId
- Dependency: $dependency
- Result: $result
- Failure Mode: $(if ($failureMode) { $failureMode } else { 'not-provided' })
- Drill Window: $(if ($drillWindow) { $drillWindow } else { 'not-provided' })
- Evidence Path: $evidencePath
- Automated Failover Triggered: $(if ($failoverTriggered) { $failoverTriggered } else { 'not-provided' })
- Automated Failback Completed: $(if ($failbackCompleted) { $failbackCompleted } else { 'not-provided' })

### Suggested Action
$recommendation

### Metadata
- Source: outage_simulation
- Related Files: references/enterprise-policy-attestation-publication-template.md, references/hook-rollout-policy-template.md
- Tags: outage-simulation, failover, policy-tuning, governance

---
"@

$policyEntry = @"
## [REC-$entryId] policy_tuning

**Logged**: $timestamp
**Source Simulation**: $simulationId
**Dependency**: $dependency
**Outcome**: $result
**Status**: suggested

### Recommendation
$recommendation

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

if ($dryRun) {
    Write-Host "[DRY-RUN] Would append to ${learningsFile}:"
    Write-Host $learningEntry
    Write-Host "[DRY-RUN] Would append to ${policyTuningFile}:"
    Write-Host $policyEntry
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

Write-Host "Appended outage simulation learning entry to $learningsFile"
Write-Host "Appended policy tuning recommendation to $policyTuningFile"
