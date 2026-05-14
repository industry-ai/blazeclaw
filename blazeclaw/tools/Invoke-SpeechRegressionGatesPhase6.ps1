param(
	[string]$ConfigPath = ".\blazeclaw.conf",
	[string]$OutputPath = ".\docs\ASR_PHASE6_REGRESSION_GATES.md",
	[double]$MaxLatencyDeltaPercent = 15.0,
	[double]$ObservedLatencyDeltaPercent = [double]::NaN,
	[double]$MaxQualityRegressionPercent = 1.0,
	[double]$ObservedQualityRegressionPercent = [double]::NaN,
	[switch]$EnforceReleaseGate
)

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$resolvedConfigPath = [System.IO.Path]::GetFullPath($ConfigPath)
$resolvedOutputPath = [System.IO.Path]::GetFullPath($OutputPath)

if (-not (Test-Path -LiteralPath $resolvedConfigPath)) {
	throw "Config file not found: $resolvedConfigPath"
}

$policyScript = Join-Path $repoRoot "tools\Test-SpeechCudaOperationalPolicy.ps1"
$matrixScript = Join-Path $repoRoot "tools\Invoke-SpeechCudaValidationMatrix.ps1"

if (-not (Test-Path -LiteralPath $policyScript)) {
	throw "Policy script not found: $policyScript"
}
if (-not (Test-Path -LiteralPath $matrixScript)) {
	throw "Matrix script not found: $matrixScript"
}

function Get-GateStatus {
	param(
		[bool]$Condition,
		[string]$PassLabel,
		[string]$FailLabel
	)

	if ($Condition) {
		return $PassLabel
	}

	return $FailLabel
}

function Is-ProvidedNumber([double]$value) {
	return -not [double]::IsNaN($value)
}

$policyOutput = & powershell -ExecutionPolicy Bypass -File $policyScript 2>&1
$policyExitCode = $LASTEXITCODE

$matrixOutput = & powershell -ExecutionPolicy Bypass -File $matrixScript -IncludePolicyCheck -IncludeInteractiveCommands 2>&1
$matrixExitCode = $LASTEXITCODE

$latencyProvided = Is-ProvidedNumber $ObservedLatencyDeltaPercent
$qualityProvided = Is-ProvidedNumber $ObservedQualityRegressionPercent

$gatePolicyPinned = ($policyExitCode -eq 0)
$gateStableProviderSelection = ($matrixExitCode -eq 0)
$gateNoFailFastCrashes = ($matrixExitCode -eq 0)
$gateLatencyDelta = $latencyProvided -and ($ObservedLatencyDeltaPercent -le $MaxLatencyDeltaPercent)
$gateQuality = $qualityProvided -and ($ObservedQualityRegressionPercent -le $MaxQualityRegressionPercent)

$releaseReady = $gatePolicyPinned -and $gateStableProviderSelection -and $gateNoFailFastCrashes -and $gateLatencyDelta -and $gateQuality

$timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss K")
$report = @()
$report += "# qwen3-asr-1.7b-onnx Phase 6 Regression Gates Report"
$report += ""
$report += "Generated: $timestamp"
$report += "Repository: $repoRoot"
$report += ""
$report += "## Inputs"
$report += "- Config path: $resolvedConfigPath"
$report += "- MaxLatencyDeltaPercent: $MaxLatencyDeltaPercent"
$report += "- ObservedLatencyDeltaPercent: $(if ($latencyProvided) { $ObservedLatencyDeltaPercent } else { 'not_provided' })"
$report += "- MaxQualityRegressionPercent: $MaxQualityRegressionPercent"
$report += "- ObservedQualityRegressionPercent: $(if ($qualityProvided) { $ObservedQualityRegressionPercent } else { 'not_provided' })"
$report += ""
$report += "## Gate Evaluation"
$report += "- no fail-fast crashes (proxy: matrix preflight): $(Get-GateStatus -Condition $gateNoFailFastCrashes -PassLabel 'pass' -FailLabel 'fail')"
$report += "- stable provider selection: $(Get-GateStatus -Condition $gateStableProviderSelection -PassLabel 'pass' -FailLabel 'fail')"
$report += "- acceptable latency delta: $(if (-not $latencyProvided) { 'manual_input_required' } else { Get-GateStatus -Condition $gateLatencyDelta -PassLabel 'pass' -FailLabel 'fail' })"
$report += "- no significant quality regression: $(if (-not $qualityProvided) { 'manual_input_required' } else { Get-GateStatus -Condition $gateQuality -PassLabel 'pass' -FailLabel 'fail' })"
$report += "- pinned CUDA/cuDNN operational policy: $(Get-GateStatus -Condition $gatePolicyPinned -PassLabel 'pass' -FailLabel 'fail')"
$report += ""
$report += "## Release Decision"
$report += "- ReleaseReady: $(if ($releaseReady) { 'yes' } else { 'no' })"
$report += "- Reason: $(if ($releaseReady) { 'all regression gates satisfied' } else { 'one or more gates are failing or missing manual metric inputs' })"
$report += ""
$report += "## Policy Check Output"
$report += "- Command: powershell -ExecutionPolicy Bypass -File .\\tools\\Test-SpeechCudaOperationalPolicy.ps1"
$report += "- ExitCode: $policyExitCode"
$report += '```text'
$report += ($policyOutput | ForEach-Object { "$_" })
$report += '```'
$report += ""
$report += "## Validation Matrix Output"
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
Write-Host "Phase 6 report written: $resolvedOutputPath"
Write-Host "ReleaseReady=$($releaseReady.ToString().ToLowerInvariant())"

if ($EnforceReleaseGate.IsPresent -and -not $releaseReady) {
	exit 2
}

exit 0
