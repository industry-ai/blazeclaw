param(
	[string]$Configuration = "Debug",
	[string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$solutionPath = Join-Path $repoRoot "BlazeClaw.sln"

Write-Host "[Phase7] Building BlazeClawMfc.Tests ($Configuration|$Platform)..."
msbuild $solutionPath /t:Build /p:Configuration=$Configuration /p:Platform=$Platform /p:CodePage=65001 /nologo | Out-Host

$testsExe = Join-Path $repoRoot "bin\$Configuration\BlazeClawMfc.Tests.exe"
if (!(Test-Path $testsExe)) {
	throw "BlazeClawMfc.Tests executable not found: $testsExe"
}

Write-Host "[Phase7] Running future stability validation tests..."
& $testsExe "[skills][future-stability][phase7]" --reporter console --success
if ($LASTEXITCODE -ne 0) {
	throw "Phase 7 future stability validation test failed. ExitCode=$LASTEXITCODE"
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$reportDir = Join-Path $repoRoot "artifacts\phase7-future-stability"
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null

$summaryPath = Join-Path $reportDir "phase7-future-stability-$timestamp.txt"
@(
	"Phase 7 future stability validation completed via test contract.",
	"Test filter: [skills][future-stability][phase7]",
	"Configuration: $Configuration",
	"Platform: $Platform",
	"Timestamp: $(Get-Date -Format s)"
) | Set-Content -Path $summaryPath -Encoding UTF8

Write-Host "[Phase7] Validation summary written: $summaryPath"
