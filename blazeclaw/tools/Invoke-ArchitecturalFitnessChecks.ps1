param(
	[string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
	[switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-LineCount {
	param([Parameter(Mandatory = $true)][string]$Path)
	return (Get-Content -Path $Path | Measure-Object -Line).Lines
}

function Assert-FileContains {
	param(
		[Parameter(Mandatory = $true)][string]$Path,
		[Parameter(Mandatory = $true)][string]$Needle,
		[Parameter(Mandatory = $true)][string]$Code
	)

	$content = Get-Content -Path $Path -Raw
	if (-not $content.Contains($Needle)) {
		throw "${Code}: missing marker '$Needle' in '$Path'"
	}
}

$resolvedRoot = (Resolve-Path $RepoRoot).Path

$thresholds = @(
	@{
		Path = "BlazeClawMfc/src/app/BlazeClawMFCView.cpp"
		MaxLines = 4600
	},
	@{
		Path = "BlazeClawMfc/src/core/ServiceManager.cpp"
		MaxLines = 3900
	},
	@{
		Path = "BlazeClawMfc/src/gateway/GatewayHost.cpp"
		MaxLines = 2200
	},
	@{
		Path = "BlazeClawMfc/src/config/ConfigLoader.cpp"
		MaxLines = 2200
	}
)

$fileMetrics = @()
foreach ($entry in $thresholds) {
	$absolutePath = Join-Path $resolvedRoot $entry.Path
	if (-not (Test-Path -Path $absolutePath)) {
		throw "FIT001: expected file not found '$absolutePath'"
	}

	$lineCount = Get-LineCount -Path $absolutePath
	if ($lineCount -gt $entry.MaxLines) {
		throw "FIT002: '$($entry.Path)' line count $lineCount exceeds max $($entry.MaxLines)"
	}

	$fileMetrics += [pscustomobject]@{
		File = $entry.Path
		Lines = $lineCount
		MaxLines = $entry.MaxLines
		RemainingHeadroom = $entry.MaxLines - $lineCount
	}
}

$surfaceAuditCpp = Join-Path $resolvedRoot "BlazeClawMfc/src/gateway/GatewayMethodSurfaceAudit.cpp"
$gatewayHostCpp = Join-Path $resolvedRoot "BlazeClawMfc/src/gateway/GatewayHost.cpp"
$surfaceAuditHeader = Join-Path $resolvedRoot "BlazeClawMfc/src/gateway/GatewayMethodSurfaceAudit.h"

Assert-FileContains -Path $surfaceAuditHeader -Needle "GatewayRuntimeMethodSurfaceInvariantsHold(" -Code "FIT101"
Assert-FileContains -Path $surfaceAuditCpp -Needle "generated_handler_catalog_subset_not_registered" -Code "FIT102"
Assert-FileContains -Path $surfaceAuditCpp -Needle "channel_handler_surface_not_registered" -Code "FIT103"
Assert-FileContains -Path $surfaceAuditCpp -Needle "plugin_rpc_surface_not_registered" -Code "FIT104"
Assert-FileContains -Path $gatewayHostCpp -Needle "GatewayRuntimeMethodSurfaceInvariantsHold(" -Code "FIT105"

Write-Host "[ArchitecturalFitness] File size trend checks"
$fileMetrics | Format-Table -AutoSize | Out-String | Write-Host

Write-Host "[ArchitecturalFitness] Route-surface and handler registration contract checks: PASS"

if (-not $ValidateOnly) {
	Write-Host "[ArchitecturalFitness] Completed."
}
