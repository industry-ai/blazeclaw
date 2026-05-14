param(
	[string]$ConfigPath = ".\blazeclaw.conf",
	[string]$OutputPath = ".\docs\ASR_PHASE5_AUDIO_PIPELINE_TUNING.md",
	[int[]]$ChunkCandidatesMs = @(320, 640, 1000, 1500),
	[int[]]$OverlapCandidatesMs = @(160, 320)
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
$currentChunk = Try-GetConfigValue -Lines $configLines -Key "speech.chunk_ms"
$currentOverlap = Try-GetConfigValue -Lines $configLines -Key "speech.overlap_ms"
$currentVariant = Try-GetConfigValue -Lines $configLines -Key "speech.model_variant"
$currentCudaEnabled = Try-GetConfigValue -Lines $configLines -Key "speech.cuda.enabled"

if ([string]::IsNullOrWhiteSpace($currentChunk)) { $currentChunk = "1000" }
if ([string]::IsNullOrWhiteSpace($currentOverlap)) { $currentOverlap = "320" }
if ([string]::IsNullOrWhiteSpace($currentVariant)) { $currentVariant = "auto" }
if ([string]::IsNullOrWhiteSpace($currentCudaEnabled)) { $currentCudaEnabled = "true" }

$rows = New-Object 'System.Collections.Generic.List[object]'
foreach ($chunk in $ChunkCandidatesMs) {
	foreach ($overlap in $OverlapCandidatesMs) {
		if ($overlap -ge $chunk) {
			continue
		}

		$targetSamples = [int]([Math]::Floor((16000 * ($chunk + $overlap)) / 1000.0))
		$frames = if ($targetSamples -ge 400) { [int](1 + [Math]::Floor(($targetSamples - 400) / 160.0)) } else { 0 }

		$rows.Add([pscustomobject]@{
			ChunkMs = $chunk
			OverlapMs = $overlap
			TargetSamples = $targetSamples
			ApproxFeatureFrames = $frames
			Valid = "yes"
		}) | Out-Null
	}
}

$recommended = $rows | Where-Object { $_.ChunkMs -eq 640 -and $_.OverlapMs -eq 160 } | Select-Object -First 1
if ($null -eq $recommended) {
	$recommended = $rows | Select-Object -First 1
}

$matrixScript = Join-Path $repoRoot "tools\Invoke-SpeechCudaValidationMatrix.ps1"
$matrixOutput = & powershell -ExecutionPolicy Bypass -File $matrixScript -IncludePolicyCheck -IncludeInteractiveCommands 2>&1
$matrixExitCode = $LASTEXITCODE

$timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss K")
$report = @()
$report += "# qwen3-asr-1.7b-onnx Phase 5 Audio Pipeline Tuning Report"
$report += ""
$report += "Generated: $timestamp"
$report += "Repository: $repoRoot"
$report += ""
$report += "## Current Runtime Config Snapshot"
$report += "- speech.chunk_ms: $currentChunk"
$report += "- speech.overlap_ms: $currentOverlap"
$report += "- speech.model_variant: $currentVariant"
$report += "- speech.cuda.enabled: $currentCudaEnabled"
$report += ""
$report += "## Candidate Audio Window Matrix"
$report += '```text'
$report += ($rows | Format-Table ChunkMs, OverlapMs, TargetSamples, ApproxFeatureFrames, Valid -AutoSize | Out-String).TrimEnd()
$report += '```'
$report += ""
$report += "## Recommendation"
$report += "- RecommendedChunkMs: $($recommended.ChunkMs)"
$report += "- RecommendedOverlapMs: $($recommended.OverlapMs)"
$report += "- Rationale: balanced low-latency and feature-window stability for streaming-first tuning"
$report += ""
$report += "## Manual Apply Commands"
$report += "- Set in blazeclaw.conf:"
$report += "  - speech.chunk_ms=$($recommended.ChunkMs)"
$report += "  - speech.overlap_ms=$($recommended.OverlapMs)"
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
Write-Host "Phase 5 report written: $resolvedOutputPath"
Write-Host "Recommended tuning: chunk=$($recommended.ChunkMs) overlap=$($recommended.OverlapMs)"
