param(
    [string]$BlazeRoot = (Resolve-Path "$PSScriptRoot\..\..\").Path,
    [string]$BuildConfig = "Debug"
)

function Find-InPath {
    param([Parameter(Mandatory = $true)][string]$FileName)

    $pathEntries = ($env:PATH -split ';') |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

    foreach ($entry in $pathEntries) {
        $candidate = Join-Path $entry $FileName
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    return $null
}

$ErrorActionPreference = "Stop"

$binDir = Join-Path $BlazeRoot "blazeclaw\bin\$BuildConfig"
$ortDll = Join-Path $binDir "onnxruntime.dll"
$providers = @(
    "onnxruntime_providers_shared.dll",
    "onnxruntime_providers_cuda.dll",
    "onnxruntime_providers_tensorrt.dll"
)
$runtimeDeps = @(
    "cublasLt64_13.dll",
    "cublas64_13.dll",
    "cufft64_12.dll",
    "cudnn64_9.dll"
)

Write-Host "[check] BlazeRoot: $BlazeRoot"
Write-Host "[check] BinDir:    $binDir"

if (-not (Test-Path $ortDll)) {
    Write-Host "[fail] onnxruntime.dll not found at: $ortDll" -ForegroundColor Red
    exit 1
}

Write-Host "[ok]   onnxruntime.dll: $ortDll" -ForegroundColor Green

foreach ($p in $providers) {
    $path = Join-Path $binDir $p
    if (Test-Path $path) {
        Write-Host "[ok]   $p present" -ForegroundColor Green
    }
    else {
        Write-Host "[warn] $p missing" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "[check] Runtime dependency lookup (PATH + app dir)"

$missing = @()
foreach ($dep in $runtimeDeps) {
    $local = Join-Path $binDir $dep
    if (Test-Path $local) {
        Write-Host "[ok]   $dep found in app dir" -ForegroundColor Green
        continue
    }

    $found = Find-InPath -FileName $dep
    if ($null -ne $found) {
        Write-Host "[ok]   $dep found in PATH -> $found" -ForegroundColor Green
    }
    else {
        Write-Host "[fail] $dep not found in app dir or PATH" -ForegroundColor Red
        $missing += $dep
    }
}

Write-Host ""
if ($missing.Count -gt 0) {
    Write-Host "[result] CUDA EP cannot load. Missing runtime DLLs:" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "         - $_" -ForegroundColor Red }
    Write-Host ""
    Write-Host "Next:" -ForegroundColor Yellow
    Write-Host "  1) Install matching CUDA Toolkit + cuDNN for your ONNX Runtime GPU build."
    Write-Host "  2) Add their bin directories to System PATH."
    Write-Host "  3) Restart Visual Studio, then re-run this script."
    exit 2
}

Write-Host "[result] Required CUDA runtime dependencies are discoverable." -ForegroundColor Green
Write-Host "If app still reports CUDA disabled, check version mismatch between ONNX Runtime GPU and CUDA/cuDNN." -ForegroundColor Yellow
exit 0
