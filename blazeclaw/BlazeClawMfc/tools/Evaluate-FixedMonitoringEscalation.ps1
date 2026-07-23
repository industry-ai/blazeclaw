#requires -Version 5.1
[CmdletBinding()]
param(
	[string] $RepoRoot = "",
	[string] $SummaryDir = "",
	[ValidateSet("L0", "L1", "L2", "L3")]
	[string] $FailOnLevel = "L2",
	[string] $OutputPath = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
}

if ([string]::IsNullOrWhiteSpace($SummaryDir)) {
	$SummaryDir = Join-Path $RepoRoot "blazeclaw\artifacts\catch2\pd-monitoring"
}

if (-not (Test-Path -LiteralPath $SummaryDir)) {
	throw "SummaryDir not found: $SummaryDir"
}

$summaryFiles = Get-ChildItem -LiteralPath $SummaryDir -Filter "catch2-summary-*.json" -File |
	Sort-Object LastWriteTime

if ($summaryFiles.Count -eq 0) {
	throw "No summary files found in: $SummaryDir"
}

$caseMap = @(
	@{ Pattern = "\[gateway\]\[parity\]\[batch34\]"; CaseId = "PD-001"; Severity = "S2" },
	@{ Pattern = "weather-email"; CaseId = "PD-002"; Severity = "S2" },
	@{ Pattern = "\[compat\]\[frontmatter\]\[parser\]\[skills\]"; CaseId = "PD-003"; Severity = "S2" },
	@{ Pattern = "\[config\]\[priority1\]"; CaseId = "PD-004"; Severity = "S2" },
	@{ Pattern = "\[parity\]\[config\]\[mfc\]\[email\]"; CaseId = "PD-005"; Severity = "S3" },
	@{ Pattern = "\[pd006\]"; CaseId = "PD-006"; Severity = "S0" },
	@{ Pattern = "\[pd007\]"; CaseId = "PD-007"; Severity = "S0" }
)

$thresholds = @{
	"S0" = @{ L1 = 1; L2 = 1; L3 = 2 }
	"S1" = @{ L1 = 1; L2 = 1; L3 = 2 }
	"S2" = @{ L1 = 1; L2 = 2; L3 = 3 }
	"S3" = @{ L1 = 1; L2 = 2; L3 = 3 }
}

function Get-LevelRank {
	param([string]$Level)
	switch ($Level) {
		"L0" { return 0 }
		"L1" { return 1 }
		"L2" { return 2 }
		"L3" { return 3 }
		default { return 0 }
	}
}

function Resolve-Case {
	param([string]$Filter)
	foreach ($entry in $caseMap) {
		if ($Filter -match $entry.Pattern) {
			return $entry
		}
	}
	return $null
}

$caseStats = @{}
$unknownFailures = @()

foreach ($file in $summaryFiles) {
	$obj = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
	$failed = ($obj.exitCode -ne 0) -or ($obj.failedAssertionsParsed -gt 0)
	if (-not $failed) {
		continue
	}

	$filter = [string]$obj.filters
	$resolved = Resolve-Case -Filter $filter
	if ($null -eq $resolved) {
		$unknownFailures += [pscustomobject]@{
			filters = $filter
			exitCode = $obj.exitCode
			summaryJson = $file.FullName
		}
		continue
	}

	if (-not $caseStats.ContainsKey($resolved.CaseId)) {
		$caseStats[$resolved.CaseId] = [pscustomobject]@{
			CaseId = $resolved.CaseId
			Severity = $resolved.Severity
			FailingRuns = 0
			SummaryFiles = New-Object System.Collections.Generic.List[string]
			MaxExitCode = 0
		}
	}

	$entry = $caseStats[$resolved.CaseId]
	$entry.FailingRuns++
	$entry.SummaryFiles.Add($file.FullName)
	if ([int]$obj.exitCode -gt [int]$entry.MaxExitCode) {
		$entry.MaxExitCode = [int]$obj.exitCode
	}
}

$rows = @()
$overallLevel = "L0"

foreach ($caseId in ($caseStats.Keys | Sort-Object)) {
	$entry = $caseStats[$caseId]
	$threshold = $thresholds[$entry.Severity]
	$level = "L0"

	if ($entry.FailingRuns -ge $threshold.L3) {
		$level = "L3"
	}
	elseif ($entry.FailingRuns -ge $threshold.L2) {
		$level = "L2"
	}
	elseif ($entry.FailingRuns -ge $threshold.L1) {
		$level = "L1"
	}

	if ((Get-LevelRank $level) -gt (Get-LevelRank $overallLevel)) {
		$overallLevel = $level
	}

	$rows += [pscustomobject]@{
		CaseId = $entry.CaseId
		Severity = $entry.Severity
		FailingRuns = $entry.FailingRuns
		EscalationLevel = $level
		MaxExitCode = $entry.MaxExitCode
		SummaryFiles = ($entry.SummaryFiles -join "; ")
	}
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss-fff"
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
	$OutputPath = Join-Path $SummaryDir "fixed-monitoring-escalation-$timestamp.json"
}

$result = [pscustomobject]@{
	generatedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
	summaryDir = $SummaryDir
	summaryFileCount = $summaryFiles.Count
	overallLevel = $overallLevel
	failOnLevel = $FailOnLevel
	caseResults = $rows
	unknownFailures = $unknownFailures
}

$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8

Write-Host "=== Fixed Monitoring Escalation Evaluation ===" -ForegroundColor Cyan
Write-Host "SummaryDir: $SummaryDir"
Write-Host "SummaryFiles: $($summaryFiles.Count)"
Write-Host "OverallLevel: $overallLevel"
Write-Host "FailOnLevel: $FailOnLevel"
Write-Host "OutputPath: $OutputPath"
Write-Host ""

if ($rows.Count -gt 0) {
	$rows | Format-Table -AutoSize CaseId, Severity, FailingRuns, EscalationLevel, MaxExitCode
}
else {
	Write-Host "No failing fixed-monitoring runs detected (L0)." -ForegroundColor Green
}

if ($unknownFailures.Count -gt 0) {
	Write-Host ""
	Write-Host "Unmapped failing filters (review case-map if intentional):" -ForegroundColor Yellow
	$unknownFailures | Format-Table -AutoSize filters, exitCode
}

if ((Get-LevelRank $overallLevel) -ge (Get-LevelRank $FailOnLevel) -and (Get-LevelRank $FailOnLevel) -gt 0) {
	exit 2
}

exit 0
