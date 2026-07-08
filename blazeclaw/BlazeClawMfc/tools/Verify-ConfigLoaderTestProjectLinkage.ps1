#requires -Version 5.1
[CmdletBinding()]
param(
	[string] $RepoRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
}

$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path

$candidateRoots = @(
	(Join-Path $RepoRoot "blazeclaw"),
	$RepoRoot
)

$resolvedProjectRoot = $null
foreach ($candidate in $candidateRoots) {
	if (-not (Test-Path -LiteralPath $candidate)) {
		continue
	}

	$mainCandidate = Join-Path $candidate "BlazeClawMfc\BlazeClawMfc.vcxproj"
	$testsCandidate = Join-Path $candidate "BlazeClawMfc.Tests\BlazeClawMfc.Tests.vcxproj"
	$configCandidate = Join-Path $candidate "BlazeClawMfc\src\config\ConfigLoader.cpp"

	if ((Test-Path -LiteralPath $mainCandidate) -and
		(Test-Path -LiteralPath $testsCandidate) -and
		(Test-Path -LiteralPath $configCandidate)) {
		$resolvedProjectRoot = $candidate
		break
	}
}

if ($null -eq $resolvedProjectRoot) {
	throw "Unable to resolve BlazeClaw project root from RepoRoot: $RepoRoot"
}

$mainProjectPath = Join-Path $resolvedProjectRoot "BlazeClawMfc\BlazeClawMfc.vcxproj"
$testsProjectPath = Join-Path $resolvedProjectRoot "BlazeClawMfc.Tests\BlazeClawMfc.Tests.vcxproj"
$configLoaderPath = Join-Path $resolvedProjectRoot "BlazeClawMfc\src\config\ConfigLoader.cpp"

foreach ($path in @($mainProjectPath, $testsProjectPath, $configLoaderPath)) {
	if (-not (Test-Path -LiteralPath $path)) {
		throw "Required path not found: $path"
	}
}

function Get-VcxprojCompileIncludes {
	param(
		[string] $ProjectPath
	)

	[xml] $project = Get-Content -LiteralPath $ProjectPath
	return @(
		$project.Project.ItemGroup.ClCompile |
			Where-Object { $_ -ne $null } |
			ForEach-Object { $_.Include } |
			Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
	)
}

$requiredHelperSources = @(
	"src\config\ConfigLoaderSpeechNormalizationHelpers.cpp",
	"src\config\ConfigLoaderSkillEntryNormalizationHelpers.cpp"
)

$configLoaderText = [System.IO.File]::ReadAllText($configLoaderPath, [System.Text.Encoding]::UTF8)
$missingIncludes = @()
foreach ($helperSource in $requiredHelperSources) {
	$headerName = [System.IO.Path]::GetFileNameWithoutExtension($helperSource) + ".h"
	if ($configLoaderText -notmatch [regex]::Escape($headerName)) {
		$missingIncludes += $headerName
	}
}

if ($missingIncludes.Count -gt 0) {
	throw "ConfigLoader.cpp is missing includes for helper headers: $($missingIncludes -join ', ')"
}

$mainIncludes = Get-VcxprojCompileIncludes -ProjectPath $mainProjectPath
$testsIncludes = Get-VcxprojCompileIncludes -ProjectPath $testsProjectPath

$failures = @()

foreach ($helperSource in $requiredHelperSources) {
	if ($mainIncludes -notcontains $helperSource) {
		$failures += "BlazeClawMfc.vcxproj missing compile unit: $helperSource"
	}

	$testsRelative = "..\\BlazeClawMfc\\$($helperSource -replace '\\', '\\')"
	$testsRelativeAlt = "..\BlazeClawMfc\$($helperSource -replace '/', '\')"
	$testsHasHelper = ($testsIncludes -contains $testsRelativeAlt) -or
		($testsIncludes | Where-Object { $_ -like "*$([System.IO.Path]::GetFileName($helperSource))" })

	if (-not $testsHasHelper) {
		$failures += "BlazeClawMfc.Tests.vcxproj missing compile unit for helper: $helperSource"
	}
}

if (-not ($testsIncludes | Where-Object { $_ -like '*ConfigLoader.cpp*' })) {
	$failures += "BlazeClawMfc.Tests.vcxproj must compile ConfigLoader.cpp directly; helper TUs must stay synchronized."
}

Write-Host "=== ConfigLoader Test Project Linkage Guard ===" -ForegroundColor Cyan
Write-Host "MainProject: $mainProjectPath"
Write-Host "TestsProject: $testsProjectPath"
Write-Host ""

if ($failures.Count -gt 0) {
	Write-Host "Linkage guard FAILED:" -ForegroundColor Red
	$failures | ForEach-Object { Write-Host "  - $_" }
	exit 2
}

Write-Host "Linkage guard PASS: ConfigLoader helper translation units are present in main and test projects." -ForegroundColor Green
exit 0
