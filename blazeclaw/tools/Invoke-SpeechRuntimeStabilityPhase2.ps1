param(
	[string]$ConfigPath = ".\blazeclaw.conf",
	[string]$OutputPath = ".\docs\ASR_PHASE2_RUNTIME_STABILITY.md",
	[switch]$ApplyRecommendedDefault
)

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$resolvedConfigPath = [System.IO.Path]::GetFullPath($ConfigPath)
$resolvedOutputPath = [System.IO.Path]::GetFullPath($OutputPath)

if (-not (Test-Path -LiteralPath $resolvedConfigPath)) {
	throw "Config file not found: $resolvedConfigPath"
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

$policyScript = Join-Path $repoRoot "tools\Test-SpeechCudaOperationalPolicy.ps1"
$matrixScript = Join-Path $repoRoot "tools\Invoke-SpeechCudaValidationMatrix.ps1"

$policyOutput = & powershell -ExecutionPolicy Bypass -File $policyScript 2>&1
$policyExitCode = $LASTEXITCODE

$matrixOutput = & powershell -ExecutionPolicy Bypass -File $matrixScript -IncludePolicyCheck -IncludeInteractiveCommands 2>&1
$matrixExitCode = $LASTEXITCODE

$recommendedMode = if ($policyExitCode -eq 0) { "aligned_cuda" } else { "forced_cpu_safety" }
$recommendedSpeechCudaEnabled = if ($recommendedMode -eq "aligned_cuda") { "true" } else { "false" }

$appliedConfigChange = "no"
if ($ApplyRecommendedDefault.IsPresent) {
	$configLines = Get-Content -LiteralPath $resolvedConfigPath
	$configLines = Upsert-ConfigEntry -Lines $configLines -Key "speech.cuda.enabled" -Value $recommendedSpeechCudaEnabled
	Set-Content -LiteralPath $resolvedConfigPath -Value $configLines -Encoding UTF8
	$appliedConfigChange = "yes"
}

$timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss K")
$report = @()
$report += "# qwen3-asr-1.7b-onnx Phase 2 Runtime Stability Report"
$report += ""
$report += "Generated: $timestamp"
$report += "Repository: $repoRoot"
$report += ""
$report += "## Phase 2 Scope"
$report += "- Keep compatibility guard + latch behavior enabled"
$report += "- Validate aligned / isolation / forced-CPU operational modes"
$report += "- Select a runtime-stable default mode for this machine"
$report += ""
$report += "## Inputs"
$report += "- Policy command: powershell -ExecutionPolicy Bypass -File .\\tools\\Test-SpeechCudaOperationalPolicy.ps1"
$report += "- Matrix command: powershell -ExecutionPolicy Bypass -File .\\tools\\Invoke-SpeechCudaValidationMatrix.ps1 -IncludePolicyCheck -IncludeInteractiveCommands"
$report += ""
$report += "## Results Summary"
$report += "- PolicyExitCode: $policyExitCode"
$report += "- MatrixExitCode: $matrixExitCode"
$report += "- RecommendedMode: $recommendedMode"
$report += "- Recommended speech.cuda.enabled: $recommendedSpeechCudaEnabled"
$report += "- Applied config change: $appliedConfigChange"
$report += ""
$report += "## Recommended Default Strategy"
if ($recommendedMode -eq "aligned_cuda") {
	$report += "- Use aligned CUDA run as default path."
	$report += "- Keep `speech.cuda.enabled=true`."
	$report += "- Use isolation and forced CPU commands as fallback diagnostics."
}
else {
	$report += "- Use forced CPU safety mode as default path until policy drift is fixed."
	$report += "- Keep `speech.cuda.enabled=false` to avoid unstable CUDA/cuDNN path selection."
	$report += "- Continue running policy check after dependency updates; re-enable CUDA only when policy passes."
}
$report += ""
$report += "## Policy Output"
$report += '```text'
$report += ($policyOutput | ForEach-Object { "$_" })
$report += '```'
$report += ""
$report += "## Matrix Output"
$report += '```text'
$report += ($matrixOutput | ForEach-Object { "$_" })
$report += '```'

$reportDir = Split-Path -Path $resolvedOutputPath -Parent
if (-not (Test-Path -LiteralPath $reportDir)) {
	New-Item -ItemType Directory -Path $reportDir | Out-Null
}

Set-Content -LiteralPath $resolvedOutputPath -Value $report -Encoding UTF8
Write-Host "Phase 2 report written: $resolvedOutputPath"
Write-Host "Recommended mode: $recommendedMode (speech.cuda.enabled=$recommendedSpeechCudaEnabled)"
