#requires -Version 5.1
[CmdletBinding()]
param(
	[string] $RepoRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
}

function Resolve-BlazeClawRoot {
	param(
		[string] $InputRoot
	)

	$candidates = @(
		(Join-Path $InputRoot "blazeclaw"),
		$InputRoot
	) | Select-Object -Unique

	foreach ($candidate in $candidates) {
		$mainPath = Join-Path $candidate "BlazeClawMfc\BlazeClawMfc.vcxproj"
		$testsPath = Join-Path $candidate "BlazeClawMfc.Tests\BlazeClawMfc.Tests.vcxproj"
		if ((Test-Path -LiteralPath $mainPath) -and (Test-Path -LiteralPath $testsPath)) {
			return (Resolve-Path $candidate).Path
		}
	}

	throw "Unable to resolve BlazeClaw solution root from RepoRoot '$InputRoot'. Checked candidates: $($candidates -join ', ')"
}

$blazeClawRoot = Resolve-BlazeClawRoot -InputRoot $RepoRoot

$mainProjectPath = Join-Path $blazeClawRoot "BlazeClawMfc\BlazeClawMfc.vcxproj"
$testsProjectPath = Join-Path $blazeClawRoot "BlazeClawMfc.Tests\BlazeClawMfc.Tests.vcxproj"
$configLoaderPath = Join-Path $blazeClawRoot "BlazeClawMfc\src\config\ConfigLoader.cpp"

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

function Test-CompileUnitIncluded {
	param(
		[string[]] $Includes,
		[string] $FileName,
		[string[]] $ExactCandidates
	)

	if ($Includes | Where-Object { $ExactCandidates -contains $_ }) {
		return $true
	}

	if ($Includes | Where-Object { $_ -like "*$FileName" }) {
		return $true
	}

	return $false
}

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

foreach ($chatSource in $requiredChatPipelineSources) {
	$chatFileName = [System.IO.Path]::GetFileName($chatSource)
	$mainCandidate = $chatSource -replace '/', '\\'
	$testsCandidate = "..\\BlazeClawMfc\\$($chatSource -replace '/', '\\')"

	if (-not (Test-CompileUnitIncluded -Includes $mainIncludes -FileName $chatFileName -ExactCandidates @($mainCandidate))) {
		$failures += "BlazeClawMfc.vcxproj missing chat-pipeline parity compile unit: $chatSource"
	}

	if (-not (Test-CompileUnitIncluded -Includes $testsIncludes -FileName $chatFileName -ExactCandidates @($testsCandidate))) {
		$failures += "BlazeClawMfc.Tests.vcxproj missing chat-pipeline parity compile unit: $chatSource"
	}
}

if (-not ($testsIncludes | Where-Object { $_ -like '*ConfigLoader.cpp*' })) {
	$failures += "BlazeClawMfc.Tests.vcxproj must compile ConfigLoader.cpp directly; helper TUs must stay synchronized."
}

Write-Host "=== ConfigLoader Test Project Linkage Guard ===" -ForegroundColor Cyan
Write-Host "BlazeClawRoot: $blazeClawRoot"
Write-Host "MainProject: $mainProjectPath"
Write-Host "TestsProject: $testsProjectPath"
Write-Host ""

if ($failures.Count -gt 0) {
	Write-Host "Linkage guard FAILED:" -ForegroundColor Red
	$failures | ForEach-Object { Write-Host "  - $_" }
	exit 2
}

Write-Host "Linkage guard PASS: ConfigLoader helper and chat-pipeline parity translation units are present in main and test projects." -ForegroundColor Green
exit 0
