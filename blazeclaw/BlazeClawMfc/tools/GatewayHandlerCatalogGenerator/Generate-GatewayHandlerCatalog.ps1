param(
    [string]$ManifestPath = "src/gateway/GatewayHandlers.manifest.json",
    [string]$OutputHeaderPath = "src/gateway/generated/GatewayHandlerCatalog.Generated.h",
    [string]$OutputSourcePath = "src/gateway/generated/GatewayHandlerCatalog.Generated.cpp"
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([string]$RelativePath)

    return Join-Path -Path $PSScriptRoot -ChildPath "../../$RelativePath"
}

function Convert-ToCppStringLiteral {
    param([string]$Value)

    $escaped = $Value.Replace('\\', '\\\\').Replace('"', '\\"')
    return '"' + $escaped + '"'
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

if ($null -eq $manifest.methods -or $null -eq $manifest.families) {
    throw "Manifest must contain 'methods' and 'families' arrays."
}

function Expand-TrioFamily {
    param($family)

    $result = New-Object System.Collections.Generic.List[string]
    foreach ($suffix in $family.suffixes) {
        if ($family.mode -eq "scopeKey") {
            if ($family.includeTools) {
                $result.Add("gateway.tools.$($suffix)ScopeKey")
            }
            $result.Add("gateway.models.$($suffix)ScopeKey")
            $result.Add("gateway.config.$($suffix)ScopeKey")
            $result.Add("gateway.transport.policy.$($suffix)ScopeKey")
            continue
        }

        if ($family.mode -eq "scopeId") {
            if ($family.includeTools) {
                $result.Add("gateway.tools.$($suffix)ScopeId")
            }
            $result.Add("gateway.models.$($suffix)ScopeId")
            $result.Add("gateway.config.$($suffix)ScopeId")
            $result.Add("gateway.transport.policy.$($suffix)ScopeId")
            continue
        }

        if ($family.mode -eq "plainKey") {
            $result.Add("gateway.models.$($suffix)Key")
            $result.Add("gateway.config.$($suffix)Key")
            $result.Add("gateway.transport.policy.$($suffix)Key")
            continue
        }

        throw "Unsupported trioFamily mode '$($family.mode)'."
    }

    return $result
}

$allMethods = New-Object System.Collections.Generic.List[string]
foreach ($method in $manifest.methods) {
    if ([string]::IsNullOrWhiteSpace($method.name)) {
        throw "Every method entry must define a non-empty 'name'."
    }

    if ([string]::IsNullOrWhiteSpace($method.kind)) {
        throw "Method '$($method.name)' must define 'kind'."
    }

    $allMethods.Add($method.name)
}

foreach ($family in $manifest.families) {
    if ($family.kind -ne "trioFamily") {
        throw "Unsupported family kind '$($family.kind)'."
    }

    $expanded = Expand-TrioFamily -family $family
    foreach ($name in $expanded) {
        $allMethods.Add($name)
    }
}

$duplicates = $allMethods |
    Group-Object |
    Where-Object { $_.Count -gt 1 } |
    Select-Object -ExpandProperty Name

if ($duplicates.Count -gt 0) {
    throw "Duplicate method names detected in manifest expansion: $($duplicates -join ', ')"
}

$orderedMethods = $allMethods | Sort-Object -Unique
$methodLines = $orderedMethods | ForEach-Object { "`t`t`"$_`"," }
$methodArrayLiteral = $methodLines -join "`r`n"

$staticMethods = @($manifest.methods | Where-Object { $_.kind -eq "static" })
$staticLines = @()
foreach ($method in $staticMethods) {
    if ([string]::IsNullOrWhiteSpace($method.payload)) {
        throw "Static method '$($method.name)' must define payload."
    }

    $methodLiteral = Convert-ToCppStringLiteral -Value $method.name
    $payloadLiteral = Convert-ToCppStringLiteral -Value $method.payload

    $staticLines += "`t`tm_dispatcher.Register($methodLiteral, [payload = std::string($payloadLiteral)](const protocol::RequestFrame& request) {"
    $staticLines += "`t`t`treturn protocol::ResponseFrame{"
    $staticLines += "`t`t`t`t.id = request.id,"
    $staticLines += "`t`t`t`t.ok = true,"
    $staticLines += "`t`t`t`t.payloadJson = payload,"
    $staticLines += "`t`t`t`t.error = std::nullopt,"
    $staticLines += "`t`t`t};"
    $staticLines += "`t`t`t});"
    $staticLines += ""
}

$staticRegistrationBody = $staticLines -join "`r`n"

$toolsMetricMethods = @($manifest.methods | Where-Object { $_.kind -eq "toolsMetric" })
$toolsMetricLines = @()
foreach ($method in $toolsMetricMethods) {
    if ([string]::IsNullOrWhiteSpace($method.template)) {
        throw "toolsMetric method '$($method.name)' must define template."
    }

    $methodLiteral = Convert-ToCppStringLiteral -Value $method.name
    $templateLiteral = Convert-ToCppStringLiteral -Value $method.template

    $toolsMetricLines += "`t`tm_dispatcher.Register($methodLiteral, [this, templateJson = std::string($templateLiteral)](const protocol::RequestFrame& request) {"
    $toolsMetricLines += "`t`t`tconst auto tools = m_toolRegistry.List();"
    $toolsMetricLines += "`t`t`tconst std::size_t toolsCount = tools.size();"
    $toolsMetricLines += "`t`t`tconst std::size_t enabledCount = static_cast<std::size_t>(std::count_if(tools.begin(), tools.end(), [](const ToolCatalogEntry& item) {"
    $toolsMetricLines += "`t`t`t`treturn item.enabled;"
    $toolsMetricLines += "`t`t`t}));"
    $toolsMetricLines += "`t`t`tconst std::size_t disabledCount = toolsCount >= enabledCount ? toolsCount - enabledCount : 0;"
    $toolsMetricLines += "`t`t`tstd::string payload = templateJson;"
    $toolsMetricLines += "`t`t	auto ReplaceToken = [&](const std::string& token, const std::string& value) {"
    $toolsMetricLines += "`t`t`t`tstd::size_t pos = 0;"
    $toolsMetricLines += "`t`t`t`twhile ((pos = payload.find(token, pos)) != std::string::npos) {"
    $toolsMetricLines += "`t`t`t`t	payload.replace(pos, token.size(), value);"
    $toolsMetricLines += "`t`t`t`t	pos += value.size();"
    $toolsMetricLines += "`t`t`t`t}"
    $toolsMetricLines += "`t`t	};"
    $toolsMetricLines += "`t`t`tReplaceToken(`"{toolsCount}`", std::to_string(toolsCount));"
    $toolsMetricLines += "`t`t`tReplaceToken(`"{enabledCount}`", std::to_string(enabledCount));"
    $toolsMetricLines += "`t`t`tReplaceToken(`"{disabledCount}`", std::to_string(disabledCount));"
    $toolsMetricLines += "`t`t	return protocol::ResponseFrame{"
    $toolsMetricLines += "`t`t`t	.id = request.id,"
    $toolsMetricLines += "`t`t`t	.ok = true,"
    $toolsMetricLines += "`t`t`t	.payloadJson = payload,"
    $toolsMetricLines += "`t`t`t	.error = std::nullopt,"
    $toolsMetricLines += "`t`t	};"
    $toolsMetricLines += "`t`t	});"
    $toolsMetricLines += ""
}

$toolsMetricRegistrationBody = $toolsMetricLines -join "`r`n"

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

$headerContent = @"
#pragma once

namespace blazeclaw::gateway {

    inline constexpr int kGatewayHandlerCatalogVersion = $($manifest.version);

} // namespace blazeclaw::gateway
"@

$sourceContent = @"
#include "pch.h"
#include "../GatewayHost.h"

#include <array>
#include <algorithm>
#include <string_view>

namespace blazeclaw::gateway {
    namespace {
        constexpr std::array<std::string_view, $($orderedMethods.Count)> kGeneratedMethodCatalog = {
$methodArrayLiteral
        };
    }

    void GatewayHost::RegisterGeneratedScopeClusterHandlers() {
        (void)kGeneratedMethodCatalog;

$staticRegistrationBody

$toolsMetricRegistrationBody

        RegisterScopeClusterHandlers();
    }

} // namespace blazeclaw::gateway
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
Write-Host "Generated catalog files from manifest version $($manifest.version)."
