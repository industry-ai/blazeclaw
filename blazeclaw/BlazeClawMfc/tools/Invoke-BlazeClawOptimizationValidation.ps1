#requires -Version 5.1
<#
.SYNOPSIS
  Runs Phase A, B, F, and E validation steps: upstream file list, duplicate method review, skill PORTING_PLAN scan, MSBuild, optional tests (Phases C/D via docs and Catch2 when `-RunTests`).

.DESCRIPTION
  Implements the workflow described in `blazeclaw/docs/architecture.md` §13 and `blazeclaw/docs/index.md` §3.

  - **Phase A:** `GatewayUpstreamDiff/Diff-OpenClawGateway.ps1` (OpenClaw gateway paths).
  - **Phase B:** `GatewayUpstreamDiff/Verify-GatewayDispatcherMethods.ps1` (duplicate `.Register("method"` review).
  - **Phase F:** `Verify-SkillPortingPlans.ps1` — every **`blazeclaw/skills/<name>/`** directory must contain **`PORTING_PLAN.md`** (see **`docs/SKILL_PORTING.md`**). Skipped with **`-SkipPhaseF`**.
  - **Phase E:** MSBuild `blazeclaw/BlazeClaw.sln` **Debug|x64** or **Release|x64** (see **`-Configuration`**) with UTF-8 code page (**`/p:CodePage=65001`**).
  - **Optional:** Run `BlazeClawMfc.Tests.exe` with CWD `blazeclaw/` (after a build, or with **`-SkipBuild`** if the test EXE already exists — same pattern as Azure Pipelines after **VSBuild**). The script resolves the test binary under **`blazeclaw\bin\<Configuration>\`** first, then common fallbacks.

  Phases **C** and **D** are **runtime rules and docs** (for example **`GATEWAY_CORE_WIRING.md`**, task-delta telemetry) — not automated here beyond Catch2 when **`-RunTests`** is set.

.EXAMPLE
  cd E:\gitRepo\blazeClaw
  .\blazeclaw\BlazeClawMfc\tools\Invoke-BlazeClawOptimizationValidation.ps1

.EXAMPLE
  .\Invoke-BlazeClawOptimizationValidation.ps1 -MsBuildPath "D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" -RunTests

.EXAMPLE
  .\Invoke-BlazeClawOptimizationValidation.ps1 -Configuration Release -SkipPhaseA -SkipPhaseB
#>
[CmdletBinding()]
param(
	[string] $RepoRoot = "",
	[string] $MsBuildPath = "",
	[string] $SolutionRelative = "blazeclaw\BlazeClaw.sln",
	[ValidateSet("Debug", "Release")]
	[string] $Configuration = "Debug",
	[switch] $SkipPhaseA,
	[switch] $SkipPhaseB,
	[switch] $SkipPhaseF,
	[switch] $SkipBuild,
	[switch] $RunTests
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
}

$diffScript = Join-Path $PSScriptRoot "GatewayUpstreamDiff\Diff-OpenClawGateway.ps1"
$verifyScript = Join-Path $PSScriptRoot "GatewayUpstreamDiff\Verify-GatewayDispatcherMethods.ps1"
$skillPlanScript = Join-Path $PSScriptRoot "Verify-SkillPortingPlans.ps1"
$solution = Join-Path $RepoRoot $SolutionRelative

function Find-MsBuild {
	$candidates = @(
		'D:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe',
		'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
		'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
		'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe'
	)
	foreach ($c in $candidates) {
		if (Test-Path -LiteralPath $c) { return $c }
	}
	return $null
}

if (-not $SkipPhaseA) {
	Write-Host "=== Phase A: OpenClaw gateway path listing ===" -ForegroundColor Cyan
	& $diffScript -RepoRoot $RepoRoot
	if (-not $?) { throw "Diff-OpenClawGateway.ps1 failed." }
}

if (-not $SkipPhaseB) {
	Write-Host ""
	Write-Host "=== Phase B: duplicate gateway method registration review ===" -ForegroundColor Cyan
	& $verifyScript -RepoRoot $RepoRoot
	if (-not $?) { throw "Verify-GatewayDispatcherMethods.ps1 failed." }
}

if (-not $SkipPhaseF) {
	Write-Host ""
	Write-Host "=== Phase F: skill PORTING_PLAN.md presence ===" -ForegroundColor Cyan
	& $skillPlanScript -RepoRoot $RepoRoot
	if (-not $?) { throw "Verify-SkillPortingPlans.ps1 failed." }
}

if (-not $SkipBuild) {
	$msb = $MsBuildPath
	if ([string]::IsNullOrWhiteSpace($msb)) {
		$msb = Find-MsBuild
	}
	if ([string]::IsNullOrWhiteSpace($msb) -or -not (Test-Path -LiteralPath $msb)) {
		throw "MSBuild not found. Pass -MsBuildPath to your MSBuild.exe (e.g. Visual Studio 18 Community)."
	}
	if (-not (Test-Path -LiteralPath $solution)) {
		throw "Solution not found: $solution"
	}
	Write-Host ""
	Write-Host "=== Phase E: MSBuild ($Configuration|x64, CodePage 65001) ===" -ForegroundColor Cyan
	& $msb $solution /t:Build /p:Configuration=$Configuration /p:Platform=x64 /p:CodePage=65001 /m
	if (-not $?) { throw "MSBuild failed (exit code $LASTEXITCODE)." }
}

function Resolve-BlazeClawTestExe {
	param([string] $Root, [string] $Cfg)
	$candidates = @(
		(Join-Path $Root "blazeclaw\bin\$Cfg\BlazeClawMfc.Tests.exe"),
		(Join-Path $Root "blazeclaw\BlazeClawMfc.Tests\bin\$Cfg\BlazeClawMfc.Tests.exe"),
		(Join-Path $Root "blazeclaw\BlazeClawMfc.Tests\x64\$Cfg\BlazeClawMfc.Tests.exe")
	)
	foreach ($p in $candidates) {
		if (Test-Path -LiteralPath $p) { return $p }
	}
	return $null
}

if ($RunTests) {
	$testExe = Resolve-BlazeClawTestExe -Root $RepoRoot -Cfg $Configuration
	if ([string]::IsNullOrWhiteSpace($testExe)) {
		Write-Warning "Tests executable not found for configuration '$Configuration'. Build BlazeClaw.sln so BlazeClawMfc.Tests lands under blazeclaw\bin\$Configuration\, or pass -Configuration to match your build."
	}
	else {
		Write-Host ""
		Write-Host "=== Catch2: BlazeClawMfc.Tests (cwd: blazeclaw/) ===" -ForegroundColor Cyan
		$blazeclawDir = Join-Path $RepoRoot "blazeclaw"
		if (-not (Test-Path -LiteralPath $blazeclawDir)) {
			throw "Expected directory for test CWD: $blazeclawDir"
		}
		Push-Location $blazeclawDir
		try {
			& $testExe
			if (-not $?) { throw "BlazeClawMfc.Tests.exe failed (exit code $LASTEXITCODE)." }
		}
		finally {
			Pop-Location
		}
	}
}

Write-Host ""
Write-Host "Invoke-BlazeClawOptimizationValidation: completed." -ForegroundColor Green
