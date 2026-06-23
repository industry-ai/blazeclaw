param(
	[string]$Configuration = "Debug",
	[string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$solutionPath = Join-Path $repoRoot "BlazeClaw.sln"

Write-Host "[Phase4] Building BlazeClawMfc.Tests ($Configuration|$Platform)..."
msbuild $solutionPath /t:Build /p:Configuration=$Configuration /p:Platform=$Platform /p:CodePage=65001 /nologo | Out-Host

$testsExe = Join-Path $repoRoot "bin\$Configuration\BlazeClawMfc.Tests.exe"
if (!(Test-Path $testsExe)) {
	throw "BlazeClawMfc.Tests executable not found: $testsExe"
}

Write-Host "[Phase4] Running catalog/prompt parity validation tests..."
& $testsExe "[skills][catalog][parity][phase4]" --reporter console --success
if ($LASTEXITCODE -ne 0) {
	throw "Phase 4 catalog/prompt parity validation test failed. ExitCode=$LASTEXITCODE"
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$reportDir = Join-Path $repoRoot "artifacts\phase4-catalog-prompt-parity"
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null

$summaryPath = Join-Path $reportDir "phase4-catalog-prompt-parity-$timestamp.txt"
@(
	"Phase 4 catalog/prompt parity validation completed via test contract.",
	"Test filter: [skills][catalog][parity][phase4]",
	"Configuration: $Configuration",
	"Platform: $Platform",
	"Timestamp: $(Get-Date -Format s)"
) | Set-Content -Path $summaryPath -Encoding UTF8

Write-Host "[Phase4] Validation summary written: $summaryPath"
