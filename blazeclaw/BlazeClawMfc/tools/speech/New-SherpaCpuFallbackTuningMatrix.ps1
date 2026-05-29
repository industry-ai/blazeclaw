param(
	[string] $OutputDirectory = "",

	[string] $BenchmarkLogDirectory = "",

	[int[]] $ThreadCounts = @(4, 2, 6, 8),

	[int[]] $PreviewChunkMs = @(500, 320),

	[int] $PreviewLookbackMs = 320,

	[string[]] $ExecutionModes = @("sequential", "parallel")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-FullPath {
	param([string] $PathValue)

	if ([string]::IsNullOrWhiteSpace($PathValue)) {
		return ""
	}

	$expanded = [Environment]::ExpandEnvironmentVariables($PathValue)
	if ([System.IO.Path]::IsPathRooted($expanded)) {
		return [System.IO.Path]::GetFullPath($expanded)
	}

	return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $expanded))
}

function Convert-ToSafeName {
	param([string] $Value)
	return ($Value -replace "[^A-Za-z0-9_.-]", "_")
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$defaultOutputDirectory = Join-Path $scriptRoot "cpu-fallback-tuning"
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
	$OutputDirectory = $defaultOutputDirectory
}

$resolvedOutputDirectory = Resolve-FullPath $OutputDirectory
New-Item -ItemType Directory -Path $resolvedOutputDirectory -Force | Out-Null

if ([string]::IsNullOrWhiteSpace($BenchmarkLogDirectory)) {
	$BenchmarkLogDirectory = Join-Path $resolvedOutputDirectory "logs"
}
$resolvedBenchmarkLogDirectory = Resolve-FullPath $BenchmarkLogDirectory
New-Item -ItemType Directory -Path $resolvedBenchmarkLogDirectory -Force | Out-Null

$summaryScript = Join-Path $scriptRoot "Invoke-SherpaProviderBenchmarkSummary.ps1"
$rows = New-Object System.Collections.Generic.List[object]
$trialIndex = 1

foreach ($chunkMs in $PreviewChunkMs) {
	foreach ($threads in $ThreadCounts) {
		foreach ($mode in $ExecutionModes) {
			$normalizedMode = $mode.Trim().ToLowerInvariant()
			if ($normalizedMode -ne "sequential" -and $normalizedMode -ne "parallel") {
				throw "Unsupported execution mode: $mode"
			}

			$trialId = "cpu-chunk$chunkMs-threads$threads-$normalizedMode"
			$safeTrialId = Convert-ToSafeName $trialId
			$overlayPath = Join-Path $resolvedOutputDirectory "$safeTrialId.conf"
			$logPath = Join-Path $resolvedBenchmarkLogDirectory "$safeTrialId.log"
			$summaryOutputDirectory = Join-Path $resolvedOutputDirectory $safeTrialId

			$overlay = @(
				"# Sherpa CPU fallback first-token tuning overlay",
				"# Trial: $trialId",
				"# Copy these values into BlazeClawMfc/blazeclaw.conf, relaunch,",
				"# record the same utterances, then save diagnostics to:",
				"# $logPath",
				"speech.cuda.enabled=false",
				"speech.streaming.enabled=true",
				"speech.streaming.chunk_ms=$chunkMs",
				"speech.streaming.preview_chunk_ms=$chunkMs",
				"speech.streaming.lookback_ms=$PreviewLookbackMs",
				"speech.streaming.preview_lookback_ms=$PreviewLookbackMs",
				"speech.threads=$threads",
				"speech.execution_mode=$normalizedMode",
				"speech.runtime_hot_mode=always_online",
				"speech.runtime_hot_warmup_enabled=true"
			)
			$overlay | Set-Content -LiteralPath $overlayPath -Encoding UTF8

			$summaryCommand = "powershell -ExecutionPolicy Bypass -File `"$summaryScript`" -LogPath `"$logPath`" -OutputDirectory `"$summaryOutputDirectory`" -ProviderLabel `"$safeTrialId`""
			$rows.Add([ordered]@{
				trial = $trialIndex
				trialId = $trialId
				previewChunkMs = $chunkMs
				previewLookbackMs = $PreviewLookbackMs
				threads = $threads
				executionMode = $normalizedMode
				overlayPath = $overlayPath
				logPath = $logPath
				summaryCommand = $summaryCommand
			}) | Out-Null

			$trialIndex++
		}
	}
}

$jsonPath = Join-Path $resolvedOutputDirectory "sherpa-cpu-fallback-tuning-matrix.json"
$markdownPath = Join-Path $resolvedOutputDirectory "sherpa-cpu-fallback-tuning-matrix.md"
$commandsPath = Join-Path $resolvedOutputDirectory "Invoke-SherpaCpuFallbackTuningSummaries.ps1"

$rows | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$markdown = New-Object System.Collections.Generic.List[string]
$markdown.Add("# Sherpa CPU Fallback Tuning Matrix") | Out-Null
$markdown.Add("") | Out-Null
$markdown.Add("Use these trials only for CPU fallback tuning. Each overlay disables CUDA") | Out-Null
$markdown.Add("so first-token latency can be compared across CPU thread counts,") | Out-Null
$markdown.Add("execution modes, and preview chunk sizes without CUDA noise.") | Out-Null
$markdown.Add("") | Out-Null
$markdown.Add("## Measurement rules") | Out-Null
$markdown.Add("") | Out-Null
$markdown.Add("- Use the same app build, model root, microphone, utterances, and room conditions.") | Out-Null
$markdown.Add("- Keep graph optimization enabled; do not change model files between trials.") | Out-Null
$markdown.Add("- Compare click-to-render and first partial timing before throughput.") | Out-Null
$markdown.Add('- Prefer `sequential` unless `parallel` proves lower first-token latency.') | Out-Null
$markdown.Add("- Archive each generated JSON and Markdown summary with the trial id.") | Out-Null
$markdown.Add("") | Out-Null
$markdown.Add("## Trials") | Out-Null
$markdown.Add("") | Out-Null
$markdown.Add("| Trial | ID | Chunk ms | Lookback ms | Threads | Mode | Overlay | Log |") | Out-Null
$markdown.Add("| --- | --- | ---: | ---: | ---: | --- | --- | --- |") | Out-Null
foreach ($row in $rows) {
	$markdown.Add(
		"| $($row.trial) | ``$($row.trialId)`` | " +
		"$($row.previewChunkMs) | $($row.previewLookbackMs) | " +
		"$($row.threads) | $($row.executionMode) | " +
		"``$($row.overlayPath)`` | ``$($row.logPath)`` |") | Out-Null
}
$markdown.Add("") | Out-Null
$markdown.Add("## Summary commands") | Out-Null
$markdown.Add("") | Out-Null
foreach ($row in $rows) {
	$markdown.Add("- ``$($row.summaryCommand)``") | Out-Null
}
$markdown | Set-Content -LiteralPath $markdownPath -Encoding UTF8

$commands = New-Object System.Collections.Generic.List[string]
$commands.Add("param()") | Out-Null
$commands.Add("") | Out-Null
$commands.Add("Set-StrictMode -Version Latest") | Out-Null
$commands.Add('$ErrorActionPreference = "Stop"') | Out-Null
$commands.Add("") | Out-Null
foreach ($row in $rows) {
	$commands.Add($row.summaryCommand) | Out-Null
}
$commands | Set-Content -LiteralPath $commandsPath -Encoding UTF8

Write-Host "CPU fallback tuning matrix JSON: $jsonPath"
Write-Host "CPU fallback tuning matrix Markdown: $markdownPath"
Write-Host "Summary command runner: $commandsPath"
Write-Host "Overlay count: $($rows.Count)"
