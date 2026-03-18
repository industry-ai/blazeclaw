param(
    [string]$ManifestPath = "src/gateway/GatewaySchemaCatalog.manifest.json",
    [string]$OutputHeaderPath = "src/gateway/generated/GatewaySchemaCatalog.Generated.h",
    [string]$OutputSourcePath = "src/gateway/generated/GatewaySchemaCatalog.Generated.cpp"
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([string]$RelativePath)

    return Join-Path -Path $PSScriptRoot -ChildPath "../../$RelativePath"
}

function Convert-ToCppStringLiteral {
    param([string]$Value)

    if ($null -eq $Value) {
        $Value = ""
    }

    $escaped = $Value.Replace('\\', '\\\\').Replace('"', '\\"')
    return '"' + $escaped + '"'
}

function Write-FileWithRetry {
    param(
        [string]$Path,
        [string]$Content,
        [int]$MaxAttempts = 20,
        [int]$SleepMs = 200
    )

    for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
        try {
            Set-Content -Path $Path -Value $Content
            return
        }
        catch {
            if ($attempt -ge $MaxAttempts) {
                throw
            }

            Start-Sleep -Milliseconds $SleepMs
        }
    }
}

$manifestFullPath = Resolve-RepoPath -RelativePath $ManifestPath
$outputHeaderFullPath = Resolve-RepoPath -RelativePath $OutputHeaderPath
$outputSourceFullPath = Resolve-RepoPath -RelativePath $OutputSourcePath

if (-not (Test-Path $manifestFullPath)) {
    throw "Manifest not found: $manifestFullPath"
}

$manifest = Get-Content -Path $manifestFullPath -Raw | ConvertFrom-Json
if ($null -eq $manifest.version) {
    throw "Manifest is missing required 'version'."
}

if ($null -eq $manifest.methods) {
    throw "Manifest must contain a 'methods' array."
}

if ($null -eq $manifest.methodPatterns) {
    $manifest | Add-Member -MemberType NoteProperty -Name methodPatterns -Value @()
}

$methodNames = @{}
foreach ($method in $manifest.methods) {
    if ([string]::IsNullOrWhiteSpace($method.name)) {
        throw "Each method rule must define a non-empty 'name'."
    }

    if ($methodNames.ContainsKey($method.name)) {
        throw "Duplicate method name detected: $($method.name)"
    }

    $methodNames[$method.name] = $true
}

$methodLines = @()
foreach ($method in $manifest.methods) {
    $nameLiteral = Convert-ToCppStringLiteral -Value $method.name
    $policyType = if ($null -ne $method.requestPolicy -and $null -ne $method.requestPolicy.type) {
        [string]$method.requestPolicy.type
    }
    else {
        ""
    }
    $policyLiteral = Convert-ToCppStringLiteral -Value $policyType
    $shapeLiteral = Convert-ToCppStringLiteral -Value ([string]$method.responseShape)
    $stringIdField = if ($null -ne $method.stringIdField) {
        [string]$method.stringIdField
    }
    else {
        ""
    }
    $stringIdLiteral = Convert-ToCppStringLiteral -Value $stringIdField

    $methodLines += "`t`t{ $nameLiteral, $policyLiteral, $shapeLiteral, $stringIdLiteral },"
}

$patternLines = @()
foreach ($patternRule in $manifest.methodPatterns) {
    if ([string]::IsNullOrWhiteSpace($patternRule.pattern)) {
        throw "Each method pattern rule must define a non-empty 'pattern'."
    }

    $patternLiteral = Convert-ToCppStringLiteral -Value $patternRule.pattern
    $policyType = if ($null -ne $patternRule.requestPolicy -and $null -ne $patternRule.requestPolicy.type) {
        [string]$patternRule.requestPolicy.type
    }
    else {
        ""
    }
    $policyLiteral = Convert-ToCppStringLiteral -Value $policyType
    $shapeLiteral = Convert-ToCppStringLiteral -Value ([string]$patternRule.responseShape)

    $patternLines += "`t`t{ $patternLiteral, $policyLiteral, $shapeLiteral },"
}

$methodArrayLiteral = if ($methodLines.Count -gt 0) {
    $methodLines -join "`r`n"
}
else {
    ""
}

$patternArrayLiteral = if ($patternLines.Count -gt 0) {
    $patternLines -join "`r`n"
}
else {
    ""
}

$headerContent = @"
#pragma once

#include <array>

namespace blazeclaw::gateway::generated {

struct SchemaMethodRule {
    const char* name;
    const char* requestPolicyType;
    const char* responseShape;
    const char* stringIdField;
};

struct SchemaMethodPatternRule {
    const char* pattern;
    const char* requestPolicyType;
    const char* responseShape;
};

inline constexpr int kGatewaySchemaCatalogVersion = $($manifest.version);

const std::array<SchemaMethodRule, $($manifest.methods.Count)>& GetSchemaMethodRules() noexcept;
const std::array<SchemaMethodPatternRule, $($manifest.methodPatterns.Count)>& GetSchemaMethodPatternRules() noexcept;

} // namespace blazeclaw::gateway::generated
"@

$sourceContent = @"
#include "pch.h"
#include "GatewaySchemaCatalog.Generated.h"

namespace blazeclaw::gateway::generated {
namespace {

constexpr std::array<SchemaMethodRule, $($manifest.methods.Count)> kSchemaMethodRules{{
$methodArrayLiteral
}};

constexpr std::array<SchemaMethodPatternRule, $($manifest.methodPatterns.Count)> kSchemaMethodPatternRules{{
$patternArrayLiteral
}};

} // namespace

const std::array<SchemaMethodRule, $($manifest.methods.Count)>& GetSchemaMethodRules() noexcept {
    return kSchemaMethodRules;
}

const std::array<SchemaMethodPatternRule, $($manifest.methodPatterns.Count)>& GetSchemaMethodPatternRules() noexcept {
    return kSchemaMethodPatternRules;
}

} // namespace blazeclaw::gateway::generated
"@

$headerDir = Split-Path -Path $outputHeaderFullPath -Parent
$sourceDir = Split-Path -Path $outputSourceFullPath -Parent

if (-not (Test-Path $headerDir)) {
    New-Item -ItemType Directory -Path $headerDir | Out-Null
}

if (-not (Test-Path $sourceDir)) {
    New-Item -ItemType Directory -Path $sourceDir | Out-Null
}

Write-FileWithRetry -Path $outputHeaderFullPath -Content $headerContent
Write-FileWithRetry -Path $outputSourceFullPath -Content $sourceContent

Write-Host "Generated schema catalog files from manifest version $($manifest.version)."
