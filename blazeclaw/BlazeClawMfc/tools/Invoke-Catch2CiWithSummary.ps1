#requires -Version 5.1
[CmdletBinding()]
param(
	[string] $RepoRoot = "",
	[ValidateSet("Debug", "Release")]
	[string] $Configuration = "Debug",
	[int64] $Seed = 2862792625,
	[string] $OutputDir = "",
	[string] $Filters = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
	$OutputDir = Join-Path $RepoRoot "artifacts\catch2"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

function Resolve-BlazeClawTestExe {
	param([string] $Root, [string] $Cfg)

	$candidates = @(
		(Join-Path $Root "blazeclaw\bin\$Cfg\BlazeClawMfc.Tests.exe"),
		(Join-Path $Root "blazeclaw\BlazeClawMfc.Tests\bin\$Cfg\BlazeClawMfc.Tests.exe"),
		(Join-Path $Root "blazeclaw\BlazeClawMfc.Tests\x64\$Cfg\BlazeClawMfc.Tests.exe")
	)

	foreach ($path in $candidates) {
		if (Test-Path -LiteralPath $path) {
			return $path
		}
	}

	return $null
}

$testExe = Resolve-BlazeClawTestExe -Root $RepoRoot -Cfg $Configuration
if ([string]::IsNullOrWhiteSpace($testExe)) {
	throw "BlazeClawMfc.Tests.exe not found for configuration '$Configuration'."
}

$workDir = Join-Path $RepoRoot "blazeclaw"
if (-not (Test-Path -LiteralPath $workDir)) {
	throw "Expected test working directory not found: $workDir"
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$rawLogPath = Join-Path $OutputDir "catch2-raw-$timestamp.log"
$summaryTxtPath = Join-Path $OutputDir "catch2-summary-$timestamp.txt"
$summaryJsonPath = Join-Path $OutputDir "catch2-summary-$timestamp.json"

$argList = @("--reporter", "compact", "--rng-seed", "$Seed")
if (-not [string]::IsNullOrWhiteSpace($Filters)) {
	$argList = @($Filters) + $argList
}

$runspec = if ([string]::IsNullOrWhiteSpace($Filters)) { "<full-suite>" } else { $Filters }

Write-Host "=== Catch2 CI Runner ===" -ForegroundColor Cyan
Write-Host "TestExe: $testExe"
Write-Host "WorkingDirectory: $workDir"
Write-Host "Seed: $Seed"
Write-Host "Filters: $runspec"
Write-Host "RawLog: $rawLogPath"

Push-Location $workDir
try {
	& $testExe @argList 2>&1 | Tee-Object -FilePath $rawLogPath
	$exitCode = $LASTEXITCODE
}
finally {
	Pop-Location
}

$text = [System.IO.File]::ReadAllText($rawLogPath, [System.Text.Encoding]::UTF8)
$text = $text -replace "`0", ""

$failureMatches = [regex]::Matches(
	$text,
	'(?m)^(?<file>[A-Za-z]:\\.*?\.cpp)\((?<line>\d+)\):\s+failed:\s+(?<msg>.*)$')

$failures = @()
foreach ($m in $failureMatches) {
	$failures += [pscustomobject]@{
		file = $m.Groups['file'].Value
		line = [int]$m.Groups['line'].Value
		message = $m.Groups['msg'].Value.Trim()
	}
}

$failedCount = $failures.Count
$rerunCommand = if ([string]::IsNullOrWhiteSpace($Filters)) {
	".\\bin\\$Configuration\\BlazeClawMfc.Tests.exe --reporter compact --rng-seed $Seed"
}
else {
	".\\bin\\$Configuration\\BlazeClawMfc.Tests.exe " + '"' + $Filters + '"' + " --reporter compact --rng-seed $Seed"
}

$summaryLines = @(
	"Catch2 Summary",
	"-------------",
	"ExitCode: $exitCode",
	"Seed: $Seed",
	"Filters: $runspec",
	"WorkingDirectory: $workDir",
	"FailedAssertionsParsed: $failedCount",
	"ReproCommand: $rerunCommand",
	"RawLog: $rawLogPath"
)

if ($failedCount -gt 0) {
	$summaryLines += ""
	$summaryLines += "Failures (file:line)"
	$summaryLines += "--------------------"
	foreach ($failure in $failures) {
		$summaryLines += ("{0}:{1} - {2}" -f $failure.file, $failure.line, $failure.message)
	}
}

Set-Content -Path $summaryTxtPath -Value $summaryLines -Encoding UTF8

$summaryObject = [pscustomobject]@{
	exitCode = $exitCode
	seed = $Seed
	filters = $runspec
	workingDirectory = $workDir
	testExe = $testExe
	failedAssertionsParsed = $failedCount
	rerunCommand = $rerunCommand
	rawLog = $rawLogPath
	summaryText = $summaryTxtPath
	failures = $failures
}

$summaryObject | ConvertTo-Json -Depth 8 | Set-Content -Path $summaryJsonPath -Encoding UTF8

Write-Host ""
Write-Host "=== Catch2 Summary ===" -ForegroundColor Yellow
Get-Content -Path $summaryTxtPath
Write-Host "SummaryJson: $summaryJsonPath"

if ($exitCode -ne 0) {
	exit $exitCode
}
