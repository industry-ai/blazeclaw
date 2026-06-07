#requires -Version 5.1
[CmdletBinding()]
param(
	[string] $RepoRoot = "",
	[string] $CsvPath = "",
	[string] $DashboardPath = "",
	[switch] $ValidateOnly
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
}

if ([string]::IsNullOrWhiteSpace($CsvPath)) {
	$CsvPath = Join-Path $RepoRoot "blazeclaw\docs\diagnostics\trackA_failure_list_normalized.csv"
}

if ([string]::IsNullOrWhiteSpace($DashboardPath)) {
	$DashboardPath = Join-Path $RepoRoot "blazeclaw\docs\diagnostics\PARITY_DRIFT_DASHBOARD.md"
}

if (-not (Test-Path -LiteralPath $CsvPath)) {
	throw "Track A CSV not found: $CsvPath"
}

if (-not (Test-Path -LiteralPath $DashboardPath)) {
	throw "Dashboard not found: $DashboardPath"
}

$rows = Import-Csv -LiteralPath $CsvPath
$grouped = $rows |
	Group-Object -Property fileLine |
	ForEach-Object {
		$seeds = ($_.Group | Select-Object -ExpandProperty seed -Unique | Sort-Object)
		$categories = ($_.Group | Select-Object -ExpandProperty errorClass -Unique)
		[pscustomobject]@{
			fileLine = $_.Name
			occurrences = $_.Count
			seeds = ($seeds -join ", ")
			categories = ($categories -join ", ")
			sampleMessage = ($_.Group[0].message)
		}
	} |
	Sort-Object -Property @{ Expression = "occurrences"; Descending = $true }, fileLine

$seedCandidates = $grouped | Where-Object { $_.occurrences -ge 2 }
$crashCandidates = $rows | Where-Object { $_.errorClass -eq "crash_defect" } |
	Group-Object -Property fileLine |
	ForEach-Object {
		$seeds = ($_.Group | Select-Object -ExpandProperty seed -Unique | Sort-Object)
		[pscustomobject]@{
			fileLine = $_.Name
			occurrences = $_.Count
			seeds = ($seeds -join ", ")
			categories = "crash_defect"
			sampleMessage = ($_.Group[0].message)
		}
	}

Write-Host "=== Parity Drift Dashboard Track A Sync ===" -ForegroundColor Cyan
Write-Host "CsvPath: $CsvPath"
Write-Host "DashboardPath: $DashboardPath"
Write-Host "Unique failures: $($grouped.Count)"
Write-Host "Recurring (occ>=2): $($seedCandidates.Count)"
Write-Host "Crash defects: $($crashCandidates.Count)"
Write-Host ""

Write-Host "Recommended dashboard seed candidates (occ>=2):" -ForegroundColor Yellow
$seedCandidates | Format-Table -AutoSize fileLine, occurrences, seeds, categories

Write-Host "Mandatory P0 crash candidates:" -ForegroundColor Yellow
$crashCandidates | Format-Table -AutoSize fileLine, occurrences, seeds

$dashboardText = [System.IO.File]::ReadAllText($DashboardPath, [System.Text.Encoding]::UTF8)
$missing = @()
foreach ($candidate in ($seedCandidates + $crashCandidates | Sort-Object fileLine -Unique)) {
	if ($dashboardText -notmatch [regex]::Escape($candidate.fileLine)) {
		$missing += $candidate.fileLine
	}
}

if ($missing.Count -gt 0) {
	Write-Host "Dashboard missing seeded file:line references:" -ForegroundColor Red
	$missing | ForEach-Object { Write-Host "  - $_" }
	if ($ValidateOnly) {
		exit 2
	}
}
else {
	Write-Host "Dashboard contains all recommended seed file:line references." -ForegroundColor Green
}

if ($ValidateOnly) {
	exit 0
}

Write-Host ""
Write-Host "Use docs/diagnostics/PARITY_DRIFT_DASHBOARD.md as the manual source of truth." -ForegroundColor Cyan
Write-Host "Update case rows when Track A baseline changes, then re-run with -ValidateOnly."
