<#
.SYNOPSIS
  Runner to execute architecture checks: file-size and boundary-drift.

.DESCRIPTION
  Collects changed files (or scans entire repo), runs file-size and
  boundary-drift checkers, aggregates JSON reports, and returns non-zero
  when any checker reports violations.

.PARAMETER RepoRoot
  Repository root path (default: current directory).

.PARAMETER Base
  Git base ref for diff detection (default: origin/main).

.PARAMETER Mode
  "changed" (default) to analyze changed files vs base, or "all" to scan all tracked files.

.PARAMETER ReportDir
  Directory to write reports (default: tools/arch_checks).

.EXAMPLE
  .\run_arch_checks.ps1 -RepoRoot . -Base origin/main -Mode changed
#>

param(
	[string]$RepoRoot = ".",
	[string]$Base = "origin/main",
	[ValidateSet("changed","all")][string]$Mode = "changed",
	[string]$Config = "tools/arch_checks/config.yml",
	[string]$ReportDir = "tools/arch_checks",
	[string]$Python = "python",
	[switch]$Audit
)

Set-StrictMode -Version Latest

function Write-Log { param($m) Write-Host "[arch_checks] $m" }

$script:root = Resolve-Path -Path $RepoRoot
$reportPath = Join-Path $script:root $ReportDir
New-Item -ItemType Directory -Path $reportPath -Force | Out-Null

Push-Location $script:root
try {
	if ($Mode -eq 'all') {
		Write-Log "Collecting all tracked files via git ls-files"
		$files = & git ls-files
	} else {
		Write-Log "Collecting changed files against base '$Base'"
		# Use three-dot range to find changes on current branch vs base
		$diffArgs = "$Base...HEAD"
		$files = & git diff --name-only $diffArgs
	}

	if (-not $files) {
		Write-Log "No files detected to check. Exiting cleanly."
		exit 0
	}

	$files = $files | Where-Object { $_ -and $_ -ne '' } | Sort-Object -Unique

	# Prepare report paths
	$fileSizeReport = (Join-Path $reportPath 'file_size_report.json')
	$boundaryReport = (Join-Path $reportPath 'boundary_report.json')
	$combinedReport = (Join-Path $reportPath 'arch_checks_report.json')

	# Run file-size checker
	Write-Log "Running file-size checker against $($files.Count) files"
	$files -join "`n" | & $Python tools/arch_checks/file_size_check.py --stdin --config $Config --repo-root $script:root --report-json $fileSizeReport
	$fsExit = $LASTEXITCODE
	Write-Log "file-size checker exit code: $fsExit"

	# Run boundary-drift checker
	Write-Log "Running boundary-drift checker against $($files.Count) files"
	$files -join "`n" | & $Python tools/arch_checks/boundary_drift_check.py --stdin --config $Config --boundary-map docs/boundary_map.yml --repo-root $script:root --report-json $boundaryReport
	$bdExit = $LASTEXITCODE
	Write-Log "boundary-drift checker exit code: $bdExit"

	if ($Audit) {
		Write-Log "Audit mode enabled: boundary-drift violations will be treated as warnings (non-fatal)."
	}

	# Aggregate reports
	# Adjust effective exit for boundary checks if audit mode is enabled
	$effectiveBoundaryExit = if ($Audit) { 0 } else { $bdExit }

	$agg = @{ summary = @{ file_size_exit = $fsExit; boundary_exit = $bdExit; effective_boundary_exit = $effectiveBoundaryExit } ; reports = @{ } }
	if (Test-Path $fileSizeReport) {
		try { $agg.reports.file_size = Get-Content $fileSizeReport -Raw | ConvertFrom-Json } catch { $agg.reports.file_size = @{ error = 'failed_to_parse' } }
	}
	if (Test-Path $boundaryReport) {
		try { $agg.reports.boundary = Get-Content $boundaryReport -Raw | ConvertFrom-Json } catch { $agg.reports.boundary = @{ error = 'failed_to_parse' } }
	}

	# Write combined JSON report
	$agg | ConvertTo-Json -Depth 5 | Out-File -FilePath $combinedReport -Encoding utf8
	Write-Log "Wrote combined report to $combinedReport"

	if ($fsExit -ne 0 -or $effectiveBoundaryExit -ne 0) {
		Write-Log "One or more checks failed. See reports for details."
		exit 1
	}

	Write-Log "All checks passed."
	exit 0

} finally {
	Pop-Location
}
