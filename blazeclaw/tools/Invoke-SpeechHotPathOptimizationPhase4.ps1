param(
	[string]$ConfigPath = ".\blazeclaw.conf",
	[string]$OutputPath = ".\docs\ASR_PHASE4_HOT_PATH_OPTIMIZATION.md"
)

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$resolvedConfigPath = [System.IO.Path]::GetFullPath($ConfigPath)
$resolvedOutputPath = [System.IO.Path]::GetFullPath($OutputPath)

if (-not (Test-Path -LiteralPath $resolvedConfigPath)) {
	throw "Config file not found: $resolvedConfigPath"
}

function Try-GetConfigValue {
	param(
		[string[]]$Lines,
		[string]$Key
	)

	$prefix = "$Key="
	foreach ($line in $Lines) {
		$trimmed = $line.Trim()
		if ($trimmed.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
			return $trimmed.Substring($prefix.Length)
		}
	}

	return $null
}

$configLines = Get-Content -LiteralPath $resolvedConfigPath
$speechEnabled = Try-GetConfigValue -Lines $configLines -Key "speech.enabled"
$speechCudaEnabled = Try-GetConfigValue -Lines $configLines -Key "speech.cuda.enabled"
$speechModelVariant = Try-GetConfigValue -Lines $configLines -Key "speech.model_variant"
$speechThreads = Try-GetConfigValue -Lines $configLines -Key "speech.threads"
$speechExecutionMode = Try-GetConfigValue -Lines $configLines -Key "speech.execution_mode"

if ([string]::IsNullOrWhiteSpace($speechEnabled)) { $speechEnabled = "true" }
if ([string]::IsNullOrWhiteSpace($speechCudaEnabled)) { $speechCudaEnabled = "true" }
if ([string]::IsNullOrWhiteSpace($speechModelVariant)) { $speechModelVariant = "auto" }
if ([string]::IsNullOrWhiteSpace($speechThreads)) { $speechThreads = "0" }
if ([string]::IsNullOrWhiteSpace($speechExecutionMode)) { $speechExecutionMode = "sequential" }

$matrixScript = Join-Path $repoRoot "tools\Invoke-SpeechCudaValidationMatrix.ps1"
$matrixOutput = & powershell -ExecutionPolicy Bypass -File $matrixScript -IncludePolicyCheck -IncludeInteractiveCommands 2>&1
$matrixExitCode = $LASTEXITCODE

$timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss K")
$report = @()
$report += "# qwen3-asr-1.7b-onnx Phase 4 Hot Path Runtime Optimization Report"
$report += ""
$report += "Generated: $timestamp"
$report += "Repository: $repoRoot"
$report += ""
$report += "## Phase 4 Scope"
$report += "- Reuse hot-path buffers to reduce per-request allocations"
$report += "- Precompute decoder input/output metadata to avoid per-step introspection"
$report += "- Keep thread/execution controls visible for speech-runtime tuning"
$report += ""
$report += "## Runtime Config Snapshot"
$report += "- speech.enabled: $speechEnabled"
$report += "- speech.cuda.enabled: $speechCudaEnabled"
$report += "- speech.model_variant: $speechModelVariant"
$report += "- speech.threads: $speechThreads"
$report += "- speech.execution_mode: $speechExecutionMode"
$report += ""
$report += "## Implemented Hot Path Changes"
$report += "- Decoder `input_names` / `output_names` are now cached at model-load time"
$report += "- Decoder fallback input tensor shapes/types are precomputed and reused"
$report += "- Decode-loop buffers (`ids`, `positionIds`, `generatedIds`) are reused with reserve/resize"
$report += "- Prompt vector reserves final size to avoid growth reallocations"
$report += "- Encoder output features are consumed in-place (copy removed)"
$report += ""
$report += "## Matrix Preflight (operational context)"
$report += "- Command: powershell -ExecutionPolicy Bypass -File .\\tools\\Invoke-SpeechCudaValidationMatrix.ps1 -IncludePolicyCheck -IncludeInteractiveCommands"
$report += "- ExitCode: $matrixExitCode"
$report += '```text'
$report += ($matrixOutput | ForEach-Object { "$_" })
$report += '```'

$reportDir = Split-Path -Path $resolvedOutputPath -Parent
if (-not (Test-Path -LiteralPath $reportDir)) {
	New-Item -ItemType Directory -Path $reportDir | Out-Null
}

Set-Content -LiteralPath $resolvedOutputPath -Value $report -Encoding UTF8
Write-Host "Phase 4 report written: $resolvedOutputPath"
