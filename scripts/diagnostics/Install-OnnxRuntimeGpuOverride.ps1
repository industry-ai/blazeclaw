param(
    [Parameter(Mandatory = $true)]
    [string]$SourceNativeDir,
    [string]$BlazeRoot = (Resolve-Path "$PSScriptRoot\..\..\").Path
)

$ErrorActionPreference = "Stop"

$source = (Resolve-Path $SourceNativeDir).Path
$target = Join-Path $BlazeRoot "blazeclaw\third_party\onnxruntime-gpu\runtimes\win-x64\native"
$solutionPath = Join-Path $BlazeRoot "blazeclaw\BlazeClaw.sln"

if (-not (Test-Path $source)) {
    throw "Source directory not found: $SourceNativeDir"
}

if (-not (Test-Path $solutionPath)) {
    throw "Invalid BlazeRoot '$BlazeRoot'. Expected solution at: $solutionPath. Pass -BlazeRoot explicitly."
}

New-Item -ItemType Directory -Path $target -Force | Out-Null

$required = @(
    "onnxruntime.dll",
    "onnxruntime_providers_shared.dll",
    "onnxruntime_providers_cuda.dll"
)

$missing = @()
foreach ($name in $required) {
    $path = Join-Path $source $name
    if (-not (Test-Path $path)) {
        $missing += $name
    }
}

if ($missing.Count -gt 0) {
    throw "Source directory is missing required files: $($missing -join ', ')"
}

Copy-Item (Join-Path $source "*.dll") -Destination $target -Force

Write-Host "[ok] Copied ONNX Runtime GPU DLLs to override directory:" -ForegroundColor Green
Write-Host "     $target"
Write-Host ""
Write-Host "Next: rebuild BlazeClaw.sln. The post-build override target will deploy these DLLs to bin output."
