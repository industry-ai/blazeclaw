#requires -Version 5.1
<#
.SYNOPSIS
  Phase F: ensure every workspace skill directory under blazeclaw/skills includes PORTING_PLAN.md.

.DESCRIPTION
  Enumerates immediate child directories of blazeclaw/skills and fails if PORTING_PLAN.md is missing.
  Aligns with blazeclaw/docs/SKILL_PORTING.md and blazeclaw/skills/readme.md.

.PARAMETER RepoRoot
  Repository root containing the blazeclaw/ folder (same convention as Invoke-BlazeClawOptimizationValidation.ps1).
  Default: three levels above this script (tools -> BlazeClawMfc -> blazeclaw -> repo root).
#>
[CmdletBinding()]
param(
	[string] $RepoRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
}

$skillsRoot = Join-Path $RepoRoot "blazeclaw\skills"
if (-not (Test-Path -LiteralPath $skillsRoot)) {
	throw "Skills root not found: $skillsRoot"
}

$missing = New-Object System.Collections.Generic.List[string]
Get-ChildItem -LiteralPath $skillsRoot -Directory |
	ForEach-Object {
		$planPath = Join-Path $_.FullName "PORTING_PLAN.md"
		if (-not (Test-Path -LiteralPath $planPath)) {
			$missing.Add($_.Name)
		}
	}

if ($missing.Count -gt 0) {
	$joined = $missing -join ", "
	throw "Phase F: missing PORTING_PLAN.md under blazeclaw/skills for: $joined"
}

$dirCount = (Get-ChildItem -LiteralPath $skillsRoot -Directory).Count
Write-Host "Phase F: PORTING_PLAN.md present for all $dirCount skill director(ies) under blazeclaw/skills." -ForegroundColor Green
