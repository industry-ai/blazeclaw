param(
    [Parameter(Mandatory = $true)]
    [string]$GeneratorScript,
    [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"

function Resolve-AbsolutePath {
    param([string]$PathValue)

    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return $PathValue
    }

    return Join-Path -Path $PSScriptRoot -ChildPath "../../$PathValue"
}

$generatorFullPath = Resolve-AbsolutePath -PathValue $GeneratorScript
if (-not (Test-Path $generatorFullPath)) {
    throw "Gateway codegen script not found: $generatorFullPath"
}

try {
    if ($VerifyOnly) {
        & $generatorFullPath -VerifyOnly
        Write-Host "Gateway codegen verify succeeded for: $GeneratorScript"
    }
    else {
        & $generatorFullPath
        Write-Host "Gateway codegen generation succeeded for: $GeneratorScript"
    }
}
catch {
    $mode = if ($VerifyOnly) { "verify" } else { "generate" }
    throw "Gateway codegen $mode failed for '$GeneratorScript'. $($_.Exception.Message)"
}
