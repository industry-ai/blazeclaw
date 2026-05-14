param(
	[string]$CudaBinPath = "D:\Program Files\bin\x64",
	[string]$CudnnBinPath = "D:\nVidia\cudnn9.20\bin\12.9\x64",
	[switch]$PassThru
)

$expected = @(
	[pscustomobject]@{ Name = "cublas64_12.dll"; Root = "cuda" },
	[pscustomobject]@{ Name = "cublasLt64_12.dll"; Root = "cuda" },
	[pscustomobject]@{ Name = "cufft64_12.dll"; Root = "cuda" },
	[pscustomobject]@{ Name = "cudnn64_9.dll"; Root = "cudnn" },
	[pscustomobject]@{ Name = "cudnn_graph64_9.dll"; Root = "cudnn" },
	[pscustomobject]@{ Name = "cudnn_engines_precompiled64_9.dll"; Root = "cudnn" },
	[pscustomobject]@{ Name = "cudnn_engines_runtime_compiled64_9.dll"; Root = "cudnn" }
)

$resolvedCudaRoot = [System.IO.Path]::GetFullPath($CudaBinPath)
$resolvedCudnnRoot = [System.IO.Path]::GetFullPath($CudnnBinPath)

$issues = New-Object 'System.Collections.Generic.List[string]'
$rows = New-Object 'System.Collections.Generic.List[object]'

if (-not (Test-Path -LiteralPath $resolvedCudaRoot)) {
	$issues.Add("Missing CUDA root: $resolvedCudaRoot") | Out-Null
}

if (-not (Test-Path -LiteralPath $resolvedCudnnRoot)) {
	$issues.Add("Missing cuDNN root: $resolvedCudnnRoot") | Out-Null
}

foreach ($entry in $expected) {
	$rootPath = if ($entry.Root -eq "cuda") { $resolvedCudaRoot } else { $resolvedCudnnRoot }
	$fullPath = Join-Path $rootPath $entry.Name
	$exists = Test-Path -LiteralPath $fullPath

	$rows.Add([pscustomobject]@{
		Dll = $entry.Name
		Root = $rootPath
		Exists = if ($exists) { "yes" } else { "no" }
	}) | Out-Null

	if (-not $exists) {
		$issues.Add("Missing required DLL: $fullPath") | Out-Null
	}
}

Write-Host "Speech CUDA operational policy check" -ForegroundColor Cyan
Write-Host "  CUDA root : $resolvedCudaRoot"
Write-Host "  cuDNN root: $resolvedCudnnRoot"
Write-Host "  Policy    : cublas/cublasLt/cufft=12, cudnn*=9"
Write-Host ""
$rows | Format-Table Dll, Root, Exists -AutoSize

if ($issues.Count -gt 0) {
	Write-Host ""
	Write-Host "Policy check FAILED" -ForegroundColor Red
	$issues | ForEach-Object { Write-Host "- $_" }
	if ($PassThru.IsPresent) {
		return [pscustomobject]@{ Passed = $false; Issues = $issues }
	}
	exit 1
}

Write-Host ""
Write-Host "Policy check PASSED" -ForegroundColor Green
if ($PassThru.IsPresent) {
	return [pscustomobject]@{ Passed = $true; Issues = @() }
}
exit 0
