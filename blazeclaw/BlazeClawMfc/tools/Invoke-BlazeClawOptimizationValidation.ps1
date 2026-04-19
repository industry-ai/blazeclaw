#requires -Version 5.1
<#
.SYNOPSIS
  Runs Phase A–F validation steps: upstream file list, duplicate method review, MSBuild, optional tests.

.DESCRIPTION
  Implements the workflow described in `blazeclaw/docs/architecture.md` §13 and `blazeclaw/docs/index.md` §3.

  - **Phase A:** `GatewayUpstreamDiff/Diff-OpenClawGateway.ps1` (OpenClaw gateway paths).
  - **Phase B:** `GatewayUpstreamDiff/Verify-GatewayDispatcherMethods.ps1` (duplicate `.Register("method"` review).
  - **Phase E:** MSBuild `blazeclaw/BlazeClaw.sln` Debug|x64 with UTF-8 code page.
  - **Optional:** Run `BlazeClawMfc.Tests.exe` after a successful build.

  Phases C, D, and F include **runtime rules and docs** (for example Phase **D** streaming semantics in `GATEWAY_CORE_WIRING.md`, Phase **F** `PORTING_PLAN.md` under `blazeclaw/skills/`) — not all are exercised by this script.

.EXAMPLE
  cd E:\gitRepo\blazeClaw
  .\blazeclaw\BlazeClawMfc\tools\Invoke-BlazeClawOptimizationValidation.ps1

.EXAMPLE
  .\Invoke-BlazeClawOptimizationValidation.ps1 -MsBuildPath "D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" -RunTests
#>
[CmdletBinding()]
param(
	[string] $RepoRoot = "",
	[string] $MsBuildPath = "",
	[string] $SolutionRelative = "blazeclaw\BlazeClaw.sln",
	[switch] $SkipPhaseA,
	[switch] $SkipPhaseB,
	[switch] $SkipBuild,
	[switch] $RunTests
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
}

$diffScript = Join-Path $PSScriptRoot "GatewayUpstreamDiff\Diff-OpenClawGateway.ps1"
$verifyScript = Join-Path $PSScriptRoot "GatewayUpstreamDiff\Verify-GatewayDispatcherMethods.ps1"
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
	Write-Host "=== Phase E: MSBuild (Debug|x64, CodePage 65001) ===" -ForegroundColor Cyan
	& $msb $solution /t:Build /p:Configuration=Debug /p:Platform=x64 /p:CodePage=65001 /m
	if (-not $?) { throw "MSBuild failed (exit code $LASTEXITCODE)." }
}

if ($RunTests -and -not $SkipBuild) {
	$testExe = Join-Path $RepoRoot "blazeclaw\bin\Debug\BlazeClawMfc.Tests.exe"
	if (-not (Test-Path -LiteralPath $testExe)) {
		$testExe = Join-Path $RepoRoot "blazeclaw\BlazeClawMfc.Tests\x64\Debug\BlazeClawMfc.Tests.exe"
	}
	if (-not (Test-Path -LiteralPath $testExe)) {
		Write-Warning "Tests executable not found (build output path may differ): $testExe"
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
