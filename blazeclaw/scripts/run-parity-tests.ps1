param(
    [string]$Configuration = "Debug",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

Write-Host "[ParityTests] Building BlazeClaw.sln ($Configuration|$Platform)..."
msbuild "BlazeClaw.sln" /t:Build /p:Configuration=$Configuration /p:Platform=$Platform

$testExe = Join-Path $repoRoot "bin/$Configuration/BlazeClawMfc.Tests.exe"
if (-not (Test-Path $testExe)) {
    throw "Parity test executable not found: $testExe"
}

Write-Host "[ParityTests] Running behavior-equivalence and parity suites..."
& $testExe "[parity]"
if ($LASTEXITCODE -ne 0) {
    throw "Parity tests failed with exit code $LASTEXITCODE"
}

Write-Host "[ParityTests] Completed successfully."
