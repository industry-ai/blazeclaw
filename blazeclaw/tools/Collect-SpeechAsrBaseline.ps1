param(
	[string]$OutputPath = ".\docs\ASR_BASELINE.md",
	[string]$ConfigPath = ".\blazeclaw.conf"
)

$resolvedOutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$resolvedConfigPath = [System.IO.Path]::GetFullPath($ConfigPath)
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))

if (-not (Test-Path -LiteralPath $resolvedConfigPath)) {
	throw "Config file not found: $resolvedConfigPath"
}

$timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss K")

$pathEntries = @($env:Path -split ';' | Where-Object { $_ -match 'nvidia|cuda|cudnn|Program Files\\bin\\x64' })

$configLines = Get-Content -LiteralPath $resolvedConfigPath
$speechLines = @($configLines | Where-Object { $_ -match '^speech\.' })
$localModelLines = @($configLines | Where-Object { $_ -match '^chat\.localModel\.' })

$policyOutput = & powershell -ExecutionPolicy Bypass -File (Join-Path $repoRoot "tools\Test-SpeechCudaOperationalPolicy.ps1") 2>&1
$policyExitCode = $LASTEXITCODE

$matrixOutput = & powershell -ExecutionPolicy Bypass -File (Join-Path $repoRoot "tools\Invoke-SpeechCudaValidationMatrix.ps1") -IncludePolicyCheck -IncludeInteractiveCommands 2>&1
$matrixExitCode = $LASTEXITCODE

$report = @()
$report += "# qwen3-asr-1.7b-onnx Baseline + Environment Lock Report"
$report += ""
$report += "Generated: $timestamp"
$report += "Repository: $repoRoot"
$report += ""
$report += "## Phase 1 Scope"
$report += "- Capture startup/runtime baseline context"
$report += "- Lock environment evidence (CUDA/cuDNN PATH + policy check)"
$report += "- Run validation preflight matrix"
$report += ""
$report += "## Config Snapshot"
$report += ""
$report += "### speech.* entries from blazeclaw.conf"
if ($speechLines.Count -eq 0) {
	$report += "- (no explicit speech.* keys in config file; runtime defaults apply)"
}
else {
	$speechLines | ForEach-Object { $report += "- $_" }
}
$report += ""
$report += "### chat.localModel.* entries from blazeclaw.conf"
if ($localModelLines.Count -eq 0) {
	$report += "- (no explicit chat.localModel.* keys found)"
}
else {
	$localModelLines | ForEach-Object { $report += "- $_" }
}
$report += ""
$report += "## CUDA/cuDNN PATH Evidence"
if ($pathEntries.Count -eq 0) {
	$report += "- (no CUDA-like PATH entries detected)"
}
else {
	$pathEntries | ForEach-Object { $report += "- $_" }
}
$report += ""
$report += "## Policy Check Result"
$report += "- Command: powershell -ExecutionPolicy Bypass -File .\\tools\\Test-SpeechCudaOperationalPolicy.ps1"
$report += "- ExitCode: $policyExitCode"
$report += ""
$report += '```text'
$report += ($policyOutput | ForEach-Object { "$_" })
$report += '```'
$report += ""
$report += "## Matrix Preflight Result"
$report += "- Command: powershell -ExecutionPolicy Bypass -File .\\tools\\Invoke-SpeechCudaValidationMatrix.ps1 -IncludePolicyCheck -IncludeInteractiveCommands"
$report += "- ExitCode: $matrixExitCode"
$report += ""
$report += '```text'
$report += ($matrixOutput | ForEach-Object { "$_" })
$report += '```'
$report += ""
$report += "## Baseline Decision"
if ($policyExitCode -ne 0) {
	$report += "- Environment lock is currently NOT compliant with pinned policy."
	$report += "- Continue with CPU-safe guarded path until required CUDA DLL policy is satisfied."
}
else {
	$report += "- Environment lock compliant with pinned policy."
}

$reportDir = Split-Path -Path $resolvedOutputPath -Parent
if (-not (Test-Path -LiteralPath $reportDir)) {
	New-Item -ItemType Directory -Path $reportDir | Out-Null
}

Set-Content -LiteralPath $resolvedOutputPath -Value $report -Encoding UTF8
Write-Host "Baseline report written: $resolvedOutputPath"
