param(
	[string]$ConfigPath = ".\blazeclaw.conf",
	[string]$OutputPath = ".\docs\ASR_PHASE3_MODEL_GRAPH_OPTIMIZATION.md",
	[switch]$ApplyRecommendedVariant
)

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$resolvedConfigPath = [System.IO.Path]::GetFullPath($ConfigPath)
$resolvedOutputPath = [System.IO.Path]::GetFullPath($OutputPath)

if (-not (Test-Path -LiteralPath $resolvedConfigPath)) {
	throw "Config file not found: $resolvedConfigPath"
}

function Normalize-Lower([string]$value) {
	if ([string]::IsNullOrWhiteSpace($value)) { return "" }
	return $value.Trim().ToLowerInvariant()
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

function Upsert-ConfigEntry {
	param(
		[string[]]$Lines,
		[string]$Key,
		[string]$Value
	)

	$prefix = "$Key="
	for ($i = 0; $i -lt $Lines.Count; $i++) {
		$trimmed = $Lines[$i].Trim()
		if ($trimmed.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
			$Lines[$i] = "$prefix$Value"
			return ,$Lines
		}
	}

	return ,($Lines + "$prefix$Value")
}

$configLines = Get-Content -LiteralPath $resolvedConfigPath
$modelPathValue = Try-GetConfigValue -Lines $configLines -Key "speech.model_path"
$storageRootValue = Try-GetConfigValue -Lines $configLines -Key "speech.storageRoot"
$variantValue = Normalize-Lower (Try-GetConfigValue -Lines $configLines -Key "speech.model_variant")

if ([string]::IsNullOrWhiteSpace($storageRootValue)) {
	$storageRootValue = "models/chat/qwen3-asr-1.7b-onnx"
}
if ([string]::IsNullOrWhiteSpace($variantValue)) {
	$variantValue = "auto"
}

$rawRoot = if (-not [string]::IsNullOrWhiteSpace($modelPathValue)) { $modelPathValue } else { $storageRootValue }

$candidates = New-Object 'System.Collections.Generic.List[string]'
if ([System.IO.Path]::IsPathRooted($rawRoot)) {
	$candidates.Add([System.IO.Path]::GetFullPath($rawRoot)) | Out-Null
}
else {
	$candidates.Add([System.IO.Path]::GetFullPath((Join-Path $repoRoot $rawRoot))) | Out-Null
	$candidates.Add([System.IO.Path]::GetFullPath((Join-Path (Join-Path $repoRoot "BlazeClawMfc") $rawRoot))) | Out-Null
}

$resolvedModelRoot = $null
foreach ($candidate in $candidates) {
	if (Test-Path -LiteralPath $candidate) {
		$resolvedModelRoot = $candidate
		break
	}
}
if ($resolvedModelRoot -eq $null -and $candidates.Count -gt 0) {
	$resolvedModelRoot = $candidates[0]
}

$variants = @(
	[pscustomobject]@{ Name = "int4"; Encoder = "encoder.int4.onnx"; DecoderInit = "decoder_init.int4.onnx"; DecoderStep = "decoder_step.int4.onnx" },
	[pscustomobject]@{ Name = "fp16"; Encoder = "encoder.fp16.onnx"; DecoderInit = "decoder_init.fp16.onnx"; DecoderStep = "decoder_step.fp16.onnx" },
	[pscustomobject]@{ Name = "fp32"; Encoder = "encoder.onnx"; DecoderInit = "decoder_init.onnx"; DecoderStep = "decoder_step.onnx" }
)

$variantRows = New-Object 'System.Collections.Generic.List[object]'
$availableVariants = New-Object 'System.Collections.Generic.List[string]'
foreach ($v in $variants) {
	$encoderExists = Test-Path -LiteralPath (Join-Path $resolvedModelRoot $v.Encoder)
	$decoderInitExists = Test-Path -LiteralPath (Join-Path $resolvedModelRoot $v.DecoderInit)
	$decoderStepExists = Test-Path -LiteralPath (Join-Path $resolvedModelRoot $v.DecoderStep)

	$isAvailable = $encoderExists -and $decoderInitExists
	if ($isAvailable) {
		$availableVariants.Add($v.Name) | Out-Null
	}

	$variantRows.Add([pscustomobject]@{
		Variant = $v.Name
		Encoder = if ($encoderExists) { "yes" } else { "no" }
		DecoderInit = if ($decoderInitExists) { "yes" } else { "no" }
		DecoderStep = if ($decoderStepExists) { "yes" } else { "no" }
		AvailableForRun = if ($isAvailable) { "yes" } else { "no" }
	}) | Out-Null
}

$recommendedVariant = if ($availableVariants.Contains("int4")) {
	"int4"
} elseif ($availableVariants.Contains("fp16")) {
	"fp16"
} elseif ($availableVariants.Contains("fp32")) {
	"fp32"
} else {
	"none"
}

$appliedVariant = "no"
if ($ApplyRecommendedVariant.IsPresent -and $recommendedVariant -ne "none") {
	$configLines = Upsert-ConfigEntry -Lines $configLines -Key "speech.model_variant" -Value $recommendedVariant
	Set-Content -LiteralPath $resolvedConfigPath -Value $configLines -Encoding UTF8
	$appliedVariant = "yes"
}

$matrixScript = Join-Path $repoRoot "tools\Invoke-SpeechCudaValidationMatrix.ps1"
$matrixOutput = & powershell -ExecutionPolicy Bypass -File $matrixScript -IncludePolicyCheck -IncludeInteractiveCommands 2>&1
$matrixExitCode = $LASTEXITCODE

$timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss K")
$report = @()
$report += "# qwen3-asr-1.7b-onnx Phase 3 Model/Graph Optimization Report"
$report += ""
$report += "Generated: $timestamp"
$report += "Repository: $repoRoot"
$report += ""
$report += "## Phase 3 Scope"
$report += "- Benchmark-ready model variant inventory (int4/fp16/fp32)"
$report += "- Graph optimization policy verification"
$report += "- Recommended variant selection for current environment"
$report += ""
$report += "## Inputs"
$report += "- Config path: $resolvedConfigPath"
$report += "- Config speech.model_variant: $variantValue"
$report += "- Resolved model root: $resolvedModelRoot"
$report += ""
$report += "## Variant Availability"
$report += '```text'
$report += ($variantRows | Format-Table Variant, Encoder, DecoderInit, DecoderStep, AvailableForRun -AutoSize | Out-String).TrimEnd()
$report += '```'
$report += ""
$report += "## Recommendation"
$report += "- RecommendedVariant: $recommendedVariant"
$report += "- AppliedVariantToConfig: $appliedVariant"
$report += "- Runtime graph optimization policy: ORT_ENABLE_ALL (enabled in speech session options)"
$report += ""
$report += "## Matrix Preflight (context for benchmark readiness)"
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
Write-Host "Phase 3 report written: $resolvedOutputPath"
Write-Host "Recommended variant: $recommendedVariant (applied=$appliedVariant)"
