#requires -Version 5.1
[CmdletBinding()]
param(
	[string] $RepoRoot = "",
	[string] $DashboardPath = "",
	[string] $SummaryDir = "",
	[string] $OutputPath = "",
	[ValidateSet("L2", "L3")]
	[string] $FailOnLevel = "L2",
	[switch] $AllowEmptySummarySet
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
	param([string] $InputRepoRoot)

	if ([string]::IsNullOrWhiteSpace($InputRepoRoot)) {
		return (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
	}

	return (Resolve-Path -LiteralPath $InputRepoRoot).Path
}

function Resolve-DashboardPath {
	param(
		[string] $Root,
		[string] $InputDashboardPath
	)

	if ([string]::IsNullOrWhiteSpace($InputDashboardPath)) {
		$candidates = @(
			(Join-Path $Root "docs\diagnostics\PARITY_DRIFT_DASHBOARD.md"),
			(Join-Path $Root "blazeclaw\docs\diagnostics\PARITY_DRIFT_DASHBOARD.md")
		)

		$resolved = $candidates |
			Where-Object { Test-Path -LiteralPath $_ } |
			Select-Object -First 1

		if (-not [string]::IsNullOrWhiteSpace($resolved)) {
			return (Resolve-Path -LiteralPath $resolved).Path
		}

		throw "Parity dashboard not found in expected locations under repo root: $Root"
	}

	if ([System.IO.Path]::IsPathRooted($InputDashboardPath)) {
		if (-not (Test-Path -LiteralPath $InputDashboardPath)) {
			throw "Dashboard path not found: $InputDashboardPath"
		}
		return (Resolve-Path -LiteralPath $InputDashboardPath).Path
	}

	$repoRelative = Join-Path $Root $InputDashboardPath
	if (-not (Test-Path -LiteralPath $repoRelative)) {
		throw "Dashboard path not found: $repoRelative"
	}

	return (Resolve-Path -LiteralPath $repoRelative).Path
}

function Resolve-SummaryDir {
	param(
		[string] $Root,
		[string] $InputSummaryDir
	)

	if ([string]::IsNullOrWhiteSpace($InputSummaryDir)) {
		return Join-Path $Root "blazeclaw\artifacts\catch2\pd-monitoring"
	}

	if ([System.IO.Path]::IsPathRooted($InputSummaryDir)) {
		return $InputSummaryDir
	}

	return Join-Path $Root $InputSummaryDir
}

function Get-DashboardCaseRows {
	param([string[]] $Lines)

	$rows = @()
	foreach ($line in $Lines) {
		if ($line -notmatch '^\|\s*PD-\d+\s*\|') {
			continue
		}

		$cells = $line.Trim().Trim('|').Split('|') | ForEach-Object { $_.Trim() }
		if ($cells.Count -lt 10) {
			continue
		}

		$currentStatus = $cells[7]
		$severity = "S3"
		$severityMatch = [regex]::Match($currentStatus, '(?i)Severity\s+S([0-9]+)')
		if ($severityMatch.Success) {
			$severity = "S$($severityMatch.Groups[1].Value)"
		}

		$rows += [pscustomobject]@{
			CaseId = $cells[0]
			CurrentStatus = $currentStatus
			NextAction = $cells[9]
			Severity = $severity
			IsFixedMonitoring = ($currentStatus -match '(?i)fixed\s*\(\s*monitoring\s*\)')
		}
	}

	return $rows
}

function Get-CaseFilterMap {
	param([string[]] $Lines)

	$map = @{}
	foreach ($line in $Lines) {
		$match = [regex]::Match($line, '^\-\s+\*\*(PD\-\d+)\*\*\s+—\s+.*filter\s+`([^`]+)`')
		if (-not $match.Success) {
			continue
		}

		$caseId = $match.Groups[1].Value
		$filter = $match.Groups[2].Value
		$map[$caseId] = $filter
	}

	return $map
}

function Get-ThresholdPolicy {
	$policy = @{}
	$policy["S0"] = [pscustomobject]@{ BlockThreshold = 1; IncidentThreshold = 2 }
	$policy["S1"] = [pscustomobject]@{ BlockThreshold = 1; IncidentThreshold = 2 }
	$policy["S2"] = [pscustomobject]@{ BlockThreshold = 2; IncidentThreshold = 3 }
	$policy["S3"] = [pscustomobject]@{ BlockThreshold = 2; IncidentThreshold = 3 }
	return $policy
}

function Resolve-EscalationLevel {
	param(
		[string] $Severity,
		[int] $FailingRuns,
		[object] $SeverityPolicy
	)

	if ($FailingRuns -le 0) {
		return "L0"
	}

	if ($FailingRuns -ge $SeverityPolicy.IncidentThreshold) {
		return "L3"
	}

	if ($FailingRuns -ge $SeverityPolicy.BlockThreshold) {
		return "L2"
	}

	return "L1"
}

$RepoRoot = Resolve-RepoRoot -InputRepoRoot $RepoRoot
$DashboardPath = Resolve-DashboardPath -Root $RepoRoot -InputDashboardPath $DashboardPath
$SummaryDir = Resolve-SummaryDir -Root $RepoRoot -InputSummaryDir $SummaryDir

$dashboardLines = [System.IO.File]::ReadAllLines($DashboardPath, [System.Text.Encoding]::UTF8)
$caseRows = Get-DashboardCaseRows -Lines $dashboardLines
$filterMap = Get-CaseFilterMap -Lines $dashboardLines
$fixedMonitoringRows = $caseRows | Where-Object { $_.IsFixedMonitoring }

if (-not (Test-Path -LiteralPath $SummaryDir)) {
	if ($AllowEmptySummarySet) {
		Write-Host "Summary directory not found, treating as empty set due to -AllowEmptySummarySet: $SummaryDir" -ForegroundColor Yellow
		$summaryFiles = @()
	}
	else {
		throw "Summary directory not found: $SummaryDir"
	}
}
else {
	$summaryFiles = Get-ChildItem -LiteralPath $SummaryDir -Filter "catch2-summary-*.json" -File | Sort-Object LastWriteTime
}

if ($summaryFiles.Count -eq 0 -and -not $AllowEmptySummarySet) {
	throw "No catch2 summary JSON files found under: $SummaryDir"
}

$summaries = @()
foreach ($file in $summaryFiles) {
	$content = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
	if ([string]::IsNullOrWhiteSpace($content)) {
		continue
	}

	$obj = $content | ConvertFrom-Json
	$obj | Add-Member -NotePropertyName "SummaryPath" -NotePropertyValue $file.FullName
	$summaries += $obj
}

$policy = Get-ThresholdPolicy

$results = @()
foreach ($row in $fixedMonitoringRows) {
	$caseId = $row.CaseId
	$severity = $row.Severity
	$filter = ""
	if ($filterMap.ContainsKey($caseId)) {
		$filter = $filterMap[$caseId]
	}

	$matchingSummaries = @()
	if (-not [string]::IsNullOrWhiteSpace($filter)) {
		$matchingSummaries = $summaries | Where-Object { $_.filters -eq $filter }
	}

	$totalRuns = $matchingSummaries.Count
	$failingRuns = ($matchingSummaries | Where-Object {
		([int]$_.exitCode -ne 0) -or ([int]$_.failedAssertionsParsed -gt 0)
	}).Count

	$severityPolicy = $policy["S3"]
	if ($policy.ContainsKey($severity)) {
		$severityPolicy = $policy[$severity]
	}

	$level = Resolve-EscalationLevel -Severity $severity -FailingRuns $failingRuns -SeverityPolicy $severityPolicy
	$action = switch ($level) {
		"L0" { "Keep monitoring cadence" }
		"L1" { "Triage owner update + attach evidence" }
		"L2" { "Block validation and move row to re-opened/open" }
		default { "Incident handling; block validation and escalate maintainers" }
	}

	$results += [pscustomobject]@{
		CaseId = $caseId
		Severity = $severity
		CurrentStatus = $row.CurrentStatus
		Filter = $filter
		TotalRuns = $totalRuns
		FailingRuns = $failingRuns
		Level = $level
		Action = $action
	}
}

$highRiskCount = ($results | Where-Object { $_.Level -eq "L2" }).Count
if ($highRiskCount -ge 2) {
	$results = $results | ForEach-Object {
		if ($_.Level -eq "L2") {
			$_.Level = "L3"
			$_.Action = "Incident handling due to multi-case correlated failures"
		}
		$_
	}
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
	if (-not (Test-Path -LiteralPath $SummaryDir)) {
		New-Item -ItemType Directory -Path $SummaryDir -Force | Out-Null
	}
	$OutputPath = Join-Path $SummaryDir "fixed-monitoring-escalation-$timestamp.json"
}
else {
	$outputDirectory = Split-Path -Path $OutputPath -Parent
	if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
		New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
	}
}

$summary = [pscustomobject]@{
	repoRoot = $RepoRoot
	dashboardPath = $DashboardPath
	summaryDir = $SummaryDir
	generatedAt = (Get-Date).ToString("o")
	policy = @{
		S0 = @{ blockThreshold = 1; incidentThreshold = 2 }
		S1 = @{ blockThreshold = 1; incidentThreshold = 2 }
		S2 = @{ blockThreshold = 2; incidentThreshold = 3 }
		S3 = @{ blockThreshold = 2; incidentThreshold = 3 }
	}
	results = $results
}

$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $OutputPath -Encoding UTF8

Write-Host "=== Fixed Monitoring Escalation Evaluation ===" -ForegroundColor Cyan
Write-Host "DashboardPath: $DashboardPath"
Write-Host "SummaryDir: $SummaryDir"
Write-Host "SummaryFiles: $($summaryFiles.Count)"
Write-Host "Output: $OutputPath"
Write-Host ""
Write-Host "Threshold policy:" -ForegroundColor Yellow
Write-Host "  S0/S1: L2 at >=1 failing run; L3 at >=2"
Write-Host "  S2/S3: L2 at >=2 failing runs; L3 at >=3"
Write-Host ""

$results | Sort-Object CaseId | Format-Table -AutoSize CaseId, Severity, TotalRuns, FailingRuns, Level, Action

$blockingLevels = @("L2", "L3")
if ($FailOnLevel -eq "L3") {
	$blockingLevels = @("L3")
}

$blocking = $results | Where-Object { $blockingLevels -contains $_.Level }
if ($blocking.Count -gt 0) {
	Write-Host ""
	Write-Host "Escalation gate BLOCKED: detected $($blocking.Count) case(s) at $($blockingLevels -join '/')." -ForegroundColor Red
	$blocking | Sort-Object CaseId | Format-Table -AutoSize CaseId, Severity, FailingRuns, Level
	Write-Host "Required action: update dashboard row status/state and attach seeded evidence artifacts." -ForegroundColor Yellow
	exit 3
}

Write-Host ""
Write-Host "Escalation gate PASS: no blocking escalation levels detected." -ForegroundColor Green
exit 0
