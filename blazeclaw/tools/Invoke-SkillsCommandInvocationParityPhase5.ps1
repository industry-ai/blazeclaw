param(
	[string]$Configuration = "Debug",
	[string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$solutionPath = Join-Path $repoRoot "BlazeClaw.sln"

Write-Host "[Phase5] Building BlazeClawMfc.Tests ($Configuration|$Platform)..."
msbuild $solutionPath /t:Build /p:Configuration=$Configuration /p:Platform=$Platform /p:CodePage=65001 /nologo | Out-Host

$testsExe = Join-Path $repoRoot "bin\$Configuration\BlazeClawMfc.Tests.exe"
if (!(Test-Path $testsExe)) {
	throw "BlazeClawMfc.Tests executable not found: $testsExe"
}

Write-Host "[Phase5] Running command invocation parity validation tests..."
& $testsExe "[skills][invocation][parity][phase5]" --reporter console --success
if ($LASTEXITCODE -ne 0) {
	throw "Phase 5 command invocation parity validation test failed. ExitCode=$LASTEXITCODE"
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$reportDir = Join-Path $repoRoot "artifacts\phase5-command-invocation-parity"
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null

$summaryPath = Join-Path $reportDir "phase5-command-invocation-parity-$timestamp.txt"
@(
	"Phase 5 command invocation parity validation completed via test contract.",
	"Test filter: [skills][invocation][parity][phase5]",
	"Configuration: $Configuration",
	"Platform: $Platform",
	"Timestamp: $(Get-Date -Format s)"
) | Set-Content -Path $summaryPath -Encoding UTF8

Write-Host "[Phase5] Validation summary written: $summaryPath"
