#requires -Version 5.1
<#
.SYNOPSIS
  Phase A upstream parity: list (and optionally diff) OpenClaw gateway sources.

.DESCRIPTION
  Use when syncing with upstream OpenClaw: review `server-methods` and `protocol`
  under `openclaw/src/gateway/`. Paths are reported relative to the repository root.

  From repo root (parent of `blazeclaw/`):
    .\blazeclaw\BlazeClawMfc\tools\GatewayUpstreamDiff\Diff-OpenClawGateway.ps1
    .\blazeclaw\BlazeClawMfc\tools\GatewayUpstreamDiff\Diff-OpenClawGateway.ps1 -GitBaseRef main

.PARAMETER RepoRoot
  Workspace root containing `openclaw/` and `blazeclaw/`. Default: four levels above this script.

.PARAMETER GitBaseRef
  If set, runs `git diff <ref> -- openclaw/src/gateway` from RepoRoot (requires git).
#>
[CmdletBinding()]
param(
	[string] $RepoRoot = "",
	[string] $GitBaseRef = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path
}

$gatewayRel = "openclaw/src/gateway"
$focusDirs = @(
	"openclaw/src/gateway/server-methods",
	"openclaw/src/gateway/protocol"
)

Write-Host "Repo root: $RepoRoot"

$gwFull = Join-Path $RepoRoot $gatewayRel
if (-not (Test-Path $gwFull)) {
	Write-Warning "Missing: $gwFull (sparse checkout or submodule — clone full OpenClaw tree to compare)."
}

foreach ($rel in $focusDirs) {
	$full = Join-Path $RepoRoot $rel
	Write-Host ""
	Write-Host "=== $rel ==="
	if (-not (Test-Path $full)) {
		Write-Warning "Missing: $full"
		continue
	}
	Get-ChildItem -Path $full -Recurse -File -ErrorAction SilentlyContinue |
		Sort-Object FullName |
		ForEach-Object {
			$_.FullName.Substring($RepoRoot.Length).TrimStart("\")
		}
}

if (-not [string]::IsNullOrWhiteSpace($GitBaseRef)) {
	Push-Location $RepoRoot
	try {
		Write-Host ""
		Write-Host "=== git diff $GitBaseRef -- $gatewayRel ==="
		git diff $GitBaseRef -- $gatewayRel
		if ($LASTEXITCODE -ne 0) {
			throw "git diff exited with code $LASTEXITCODE"
		}
	}
	finally {
		Pop-Location
	}
}
