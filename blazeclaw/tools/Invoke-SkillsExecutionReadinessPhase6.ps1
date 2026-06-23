param(
	[string]$Configuration = "Debug",
	[string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$solutionPath = Join-Path $repoRoot "BlazeClaw.sln"

Write-Host "[Phase6] Building BlazeClawMfc.Tests ($Configuration|$Platform)..."
msbuild $solutionPath /t:Build /p:Configuration=$Configuration /p:Platform=$Platform /p:CodePage=65001 /nologo | Out-Host

$testsExe = Join-Path $repoRoot "bin\$Configuration\BlazeClawMfc.Tests.exe"
if (!(Test-Path $testsExe)) {
	throw "BlazeClawMfc.Tests executable not found: $testsExe"
}

Write-Host "[Phase6] Running execution readiness validation tests..."
& $testsExe "[skills][execution-readiness][phase6]" --reporter console --success
if ($LASTEXITCODE -ne 0) {
	throw "Phase 6 execution readiness validation test failed. ExitCode=$LASTEXITCODE"
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$reportDir = Join-Path $repoRoot "artifacts\phase6-execution-readiness"
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null

$summaryPath = Join-Path $reportDir "phase6-execution-readiness-$timestamp.txt"
@(
	"Phase 6 execution readiness validation completed via test contract.",
	"Test filter: [skills][execution-readiness][phase6]",
	"Configuration: $Configuration",
	"Platform: $Platform",
	"Timestamp: $(Get-Date -Format s)"
) | Set-Content -Path $summaryPath -Encoding UTF8

Write-Host "[Phase6] Validation summary written: $summaryPath"
