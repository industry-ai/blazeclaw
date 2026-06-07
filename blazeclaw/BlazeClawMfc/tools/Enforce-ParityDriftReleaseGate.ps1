#requires -Version 5.1
[CmdletBinding()]
param(
	[string] $RepoRoot = "",
	[string] $DashboardPath = "",
	[switch] $AllowOpenP0P1,
	[string] $OverrideReason = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
}

if ([string]::IsNullOrWhiteSpace($DashboardPath)) {
	$DashboardPath = Join-Path $RepoRoot "blazeclaw\docs\diagnostics\PARITY_DRIFT_DASHBOARD.md"
}

if (-not (Test-Path -LiteralPath $DashboardPath)) {
	throw "Parity drift dashboard not found: $DashboardPath"
}

$lines = [System.IO.File]::ReadAllLines($DashboardPath, [System.Text.Encoding]::UTF8)
$caseRows = $lines | Where-Object { $_ -match '^\|\s*PD-\d+\s*\|' }

$openP0P1 = @()

foreach ($row in $caseRows) {
	$cells = $row.Trim().Trim('|').Split('|') | ForEach-Object { $_.Trim() }
	if ($cells.Count -lt 10) {
		continue
	}

	$caseId = $cells[0]
	$currentStatus = $cells[7]
	$nextAction = $cells[9]

	$hasOpenState = $currentStatus -match '(?i)\b(open|re-opened)\b'
	if (-not $hasOpenState) {
		continue
	}

	$priorityMatch = [regex]::Match($currentStatus, '(?i)\bPriority\s+P([0-9]+)\b')
	if (-not $priorityMatch.Success) {
		continue
	}

	$priority = [int]$priorityMatch.Groups[1].Value
	if ($priority -gt 1) {
		continue
	}

	$severity = "unknown"
	$severityMatch = [regex]::Match($currentStatus, '(?i)\bSeverity\s+S([0-9]+)\b')
	if ($severityMatch.Success) {
		$severity = "S$($severityMatch.Groups[1].Value)"
	}

	$openP0P1 += [pscustomobject]@{
		CaseId = $caseId
		CurrentStatus = $currentStatus
		Severity = $severity
		Priority = "P$priority"
		NextAction = $nextAction
	}
}

Write-Host "=== Parity Drift Release Gate ===" -ForegroundColor Cyan
Write-Host "DashboardPath: $DashboardPath"
Write-Host "TotalCaseRows: $($caseRows.Count)"
Write-Host "OpenP0P1Cases: $($openP0P1.Count)"
Write-Host ""

if ($openP0P1.Count -eq 0) {
	Write-Host "Release gate PASS: no open P0/P1 parity drift cases." -ForegroundColor Green
	exit 0
}

Write-Host "Release gate BLOCKED: open P0/P1 parity drift cases require explicit status movement before branch cut." -ForegroundColor Red
$openP0P1 | Format-Table -AutoSize CaseId, Priority, Severity, CurrentStatus
Write-Host ""
Write-Host "Required action: move case status in PARITY_DRIFT_DASHBOARD.md (for example fixed/monitoring/closed) before cutting release branch." -ForegroundColor Yellow

if (-not $AllowOpenP0P1) {
	exit 2
}

if ([string]::IsNullOrWhiteSpace($OverrideReason)) {
	throw "Override requested via -AllowOpenP0P1 but no -OverrideReason provided."
}

Write-Host ""
Write-Host "Override accepted (manual release exception)." -ForegroundColor Yellow
Write-Host "OverrideReason: $OverrideReason"
Write-Host "Result: PASS with override; follow-up status movement remains mandatory post-cut." -ForegroundColor Yellow
exit 0
