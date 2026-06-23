param(
	[string]$Configuration = "Debug",
	[string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$solutionPath = Join-Path $repoRoot "BlazeClaw.sln"

Write-Host "[Phase3] Building BlazeClawMfc.Tests ($Configuration|$Platform)..."
msbuild $solutionPath /t:Build /p:Configuration=$Configuration /p:Platform=$Platform /p:CodePage=65001 /nologo | Out-Host

$testsExe = Join-Path $repoRoot "bin\$Configuration\BlazeClawMfc.Tests.exe"
if (!(Test-Path $testsExe)) {
	throw "BlazeClawMfc.Tests executable not found: $testsExe"
}

Write-Host "[Phase3] Running inventory validation tests..."
& $testsExe "[skills][inventory][phase3]" --reporter console --success
if ($LASTEXITCODE -ne 0) {
	throw "Phase 3 inventory validation test failed. ExitCode=$LASTEXITCODE"
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$reportDir = Join-Path $repoRoot "artifacts\phase3-inventory-validation"
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null

$summaryPath = Join-Path $reportDir "phase3-inventory-validation-$timestamp.txt"
@(
	"Phase 3 inventory validation completed via test contract.",
	"Test filter: [skills][inventory][phase3]",
	"Configuration: $Configuration",
	"Platform: $Platform",
	"Timestamp: $(Get-Date -Format s)"
) | Set-Content -Path $summaryPath -Encoding UTF8

Write-Host "[Phase3] Validation summary written: $summaryPath"
