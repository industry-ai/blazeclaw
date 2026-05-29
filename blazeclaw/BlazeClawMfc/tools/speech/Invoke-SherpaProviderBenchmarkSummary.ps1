param(
	[Parameter(Mandatory = $true)]
	[string] $LogPath,

	[string] $OutputDirectory = "",

	[string] $Scenario = "sherpa-live-preview",

	[string] $ProviderLabel = "auto",

	[switch] $RequireCudaActive
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

function New-EmptyMetricSet {
	return [ordered]@{
		scenario = $Scenario
		providerLabel = $ProviderLabel
		effectiveProvider = ""
		cudaAvailable = $null
		cudaEnabled = $null
		cudaReason = ""
		modelLoadStage = ""
		modelLoadLatencyMs = $null
		warmupCompleted = $null
		warmupSucceeded = $null
		warmupProvider = ""
		warmupStage = ""
		warmupLatencyMs = $null
		streamingLatencyProfile = ""
		speechThreads = $null
		speechExecutionMode = ""
		streamingPreviewChunkMs = $null
		streamingPreviewLookbackMs = $null
		firstEncoderStartOffsetMs = $null
		firstEncoderEndOffsetMs = $null
		firstDecoderStartOffsetMs = $null
		firstJoinerStartOffsetMs = $null
		firstPartialTextOffsetMs = $null
		nativePayloadReadyOffsetMs = $null
		gatewayNativePayloadReadyOffsetMs = $null
		clickToPreviewRequestMs = $null
		clickToPreviewResponseMs = $null
		clickToRenderMs = $null
		previewResponseToRenderMs = $null
		steadyStatePartialIntervalMs = $null
		matchedLineCount = 0
	}
}

function Convert-ToBoolOrNull {
	param([string] $Value)

	if ($Value -eq "true") { return $true }
	if ($Value -eq "false") { return $false }
	return $null
}

function Set-IfNumberMatch {
	param(
		[System.Collections.IDictionary] $Metrics,
		[string] $Name,
		[string] $Text,
		[string] $Pattern
	)

	$match = [regex]::Match($Text, $Pattern)
	if ($match.Success) {
		$Metrics[$Name] = [int64]$match.Groups[1].Value
	}
}

function Set-IfStringMatch {
	param(
		[System.Collections.IDictionary] $Metrics,
		[string] $Name,
		[string] $Text,
		[string] $Pattern
	)

	$match = [regex]::Match($Text, $Pattern)
	if ($match.Success) {
		$Metrics[$Name] = [string]$match.Groups[1].Value
	}
}

$resolvedLogPath = Resolve-FullPath $LogPath
if (-not (Test-Path -LiteralPath $resolvedLogPath -PathType Leaf)) {
	throw "Log file not found: $resolvedLogPath"
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
	$OutputDirectory = Join-Path (Split-Path -Parent $resolvedLogPath) "speech-provider-benchmark"
}
$resolvedOutputDirectory = Resolve-FullPath $OutputDirectory
New-Item -ItemType Directory -Path $resolvedOutputDirectory -Force | Out-Null

$metrics = New-EmptyMetricSet
$lines = Get-Content -LiteralPath $resolvedLogPath -Encoding UTF8
$previewResponseTimes = New-Object System.Collections.Generic.List[double]

foreach ($line in $lines) {
	$text = [string]$line
	$isRelevant = $false

	if ($text -match "startup\.runtime") {
		$isRelevant = $true
		Set-IfStringMatch $metrics "effectiveProvider" $text "effectiveProvider=([^\s]+)"
		Set-IfNumberMatch $metrics "speechThreads" $text "threads=(\d+)"
		Set-IfStringMatch $metrics "speechExecutionMode" $text "mode=([^\s]+)"
		Set-IfStringMatch $metrics "modelLoadStage" $text "startup\.runtime\.load - stage=([^\s]+)"
		Set-IfNumberMatch $metrics "modelLoadLatencyMs" $text "startup\.runtime\.load - .*latencyMs=(\d+)"
		Set-IfStringMatch $metrics "warmupProvider" $text "startup\.runtime\.warmup - .*provider=([^\s]+)"
		Set-IfStringMatch $metrics "warmupStage" $text "startup\.runtime\.warmup - .*stage=([^\s]+)"
		Set-IfNumberMatch $metrics "warmupLatencyMs" $text "startup\.runtime\.warmup - .*latencyMs=(\d+)"

		$cudaMatch = [regex]::Match($text, "startup\.runtime\.cuda - available=([^\s]+) enabled=([^\s]+) reason=([^\s]+)")
		if ($cudaMatch.Success) {
			$metrics.cudaAvailable = Convert-ToBoolOrNull $cudaMatch.Groups[1].Value
			$metrics.cudaEnabled = Convert-ToBoolOrNull $cudaMatch.Groups[2].Value
			$metrics.cudaReason = $cudaMatch.Groups[3].Value
		}

		$warmupMatch = [regex]::Match($text, "startup\.runtime\.warmup - .*completed=([^\s]+) succeeded=([^\s]+)")
		if ($warmupMatch.Success) {
			$metrics.warmupCompleted = Convert-ToBoolOrNull $warmupMatch.Groups[1].Value
			$metrics.warmupSucceeded = Convert-ToBoolOrNull $warmupMatch.Groups[2].Value
		}
	}

	if ($text -match "startup\.config") {
		$isRelevant = $true
		Set-IfStringMatch $metrics "streamingLatencyProfile" $text "streamingLatencyProfile=([^\s]+)"
		Set-IfNumberMatch $metrics "speechThreads" $text "threads=(\d+)"
		Set-IfStringMatch $metrics "speechExecutionMode" $text "mode=([^\s]+)"
		Set-IfNumberMatch $metrics "streamingPreviewChunkMs" $text "streamingPreviewChunkMs=(\d+)"
		Set-IfNumberMatch $metrics "streamingPreviewLookbackMs" $text "streamingPreviewLookbackMs=(\d+)"
	}

	if ($text -match "speech-preview-diagnostic" -or $text -match "firstTokenTiming") {
		$isRelevant = $true
		Set-IfStringMatch $metrics "effectiveProvider" $text 'effectiveExecutionProvider[''" ]*[:=][''" ]*([A-Za-z0-9_\-]+)'
		Set-IfNumberMatch $metrics "speechThreads" $text 'threads[''" ]*[:=]\s*(\d+)'
		Set-IfStringMatch $metrics "speechExecutionMode" $text 'executionMode[''" ]*[:=][''" ]*([A-Za-z0-9_\-]+)'
		Set-IfNumberMatch $metrics "firstEncoderStartOffsetMs" $text 'firstTokenEncoderStartOffsetMs[''" ]*[:=]\s*(\d+)'
		Set-IfNumberMatch $metrics "firstEncoderEndOffsetMs" $text 'firstTokenEncoderEndOffsetMs[''" ]*[:=]\s*(\d+)'
		Set-IfNumberMatch $metrics "firstDecoderStartOffsetMs" $text 'firstTokenDecoderStartOffsetMs[''" ]*[:=]\s*(\d+)'
		Set-IfNumberMatch $metrics "firstJoinerStartOffsetMs" $text 'firstTokenJoinerStartOffsetMs[''" ]*[:=]\s*(\d+)'
		Set-IfNumberMatch $metrics "firstPartialTextOffsetMs" $text 'firstTokenPartialTextOffsetMs[''" ]*[:=]\s*(\d+)'
		Set-IfNumberMatch $metrics "nativePayloadReadyOffsetMs" $text 'firstTokenNativePayloadReadyOffsetMs[''" ]*[:=]\s*(\d+)'
		Set-IfNumberMatch $metrics "gatewayNativePayloadReadyOffsetMs" $text 'gatewayNativePayloadReadyOffsetMs[''" ]*[:=]\s*(\d+)'
		Set-IfNumberMatch $metrics "clickToPreviewRequestMs" $text 'clickToPreviewRequestMs[''" ]*[:=]\s*(\d+)'
		Set-IfNumberMatch $metrics "clickToPreviewResponseMs" $text 'clickToPreviewResponseMs[''" ]*[:=]\s*(\d+)'
		Set-IfNumberMatch $metrics "clickToRenderMs" $text 'clickToRenderMs[''" ]*[:=]\s*(\d+)'
		Set-IfNumberMatch $metrics "previewResponseToRenderMs" $text 'previewResponseToRenderMs[''" ]*[:=]\s*(\d+)'

		$responseMatch = [regex]::Match($text, 'clickToPreviewResponseMs[''" ]*[:=]\s*(\d+(?:\.\d+)?)')
		if ($responseMatch.Success) {
			$previewResponseTimes.Add([double]$responseMatch.Groups[1].Value)
		}
	}

	if ($isRelevant) {
		$metrics.matchedLineCount = [int]$metrics.matchedLineCount + 1
	}
}

if ($previewResponseTimes.Count -ge 2) {
	$intervals = New-Object System.Collections.Generic.List[double]
	for ($i = 1; $i -lt $previewResponseTimes.Count; $i++) {
		$delta = $previewResponseTimes[$i] - $previewResponseTimes[$i - 1]
		if ($delta -gt 0) {
			$intervals.Add($delta)
		}
	}

	if ($intervals.Count -gt 0) {
		$metrics.steadyStatePartialIntervalMs = [math]::Round(($intervals | Measure-Object -Average).Average, 2)
	}
}

if ($RequireCudaActive) {
	if ($metrics.effectiveProvider -ne "cuda" -or
		$metrics.cudaAvailable -ne $true -or
		$metrics.cudaEnabled -ne $true -or
		$metrics.cudaReason -ne "active") {
		Write-Warning "CUDA active requirement was not met. Align CUDA/cuDNN DLL roots before using this run as CUDA benchmark data."
	}
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$safeProvider = ($ProviderLabel -replace "[^A-Za-z0-9_.-]", "_")
$jsonPath = Join-Path $resolvedOutputDirectory "speech-provider-benchmark-$safeProvider-$stamp.json"
$mdPath = Join-Path $resolvedOutputDirectory "speech-provider-benchmark-$safeProvider-$stamp.md"

$metrics | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $jsonPath -Encoding UTF8

$markdown = @(
	"# Sherpa Provider Benchmark Summary",
	"",
	"- Scenario: $($metrics.scenario)",
	"- Provider label: $($metrics.providerLabel)",
	"- Effective provider: $($metrics.effectiveProvider)",
	"- CUDA: available=$($metrics.cudaAvailable) enabled=$($metrics.cudaEnabled) reason=$($metrics.cudaReason)",
	"- CPU tuning: threads=$($metrics.speechThreads) mode=$($metrics.speechExecutionMode)",
	"- Model load: stage=$($metrics.modelLoadStage) latencyMs=$($metrics.modelLoadLatencyMs)",
	"- Warmup: completed=$($metrics.warmupCompleted) succeeded=$($metrics.warmupSucceeded) provider=$($metrics.warmupProvider) stage=$($metrics.warmupStage) latencyMs=$($metrics.warmupLatencyMs)",
	"- Streaming profile: $($metrics.streamingLatencyProfile) previewChunkMs=$($metrics.streamingPreviewChunkMs) previewLookbackMs=$($metrics.streamingPreviewLookbackMs)",
	"- First encoder: startMs=$($metrics.firstEncoderStartOffsetMs) endMs=$($metrics.firstEncoderEndOffsetMs)",
	"- First decoder/joiner: decoderStartMs=$($metrics.firstDecoderStartOffsetMs) joinerStartMs=$($metrics.firstJoinerStartOffsetMs)",
	"- First partial/native payload: partialTextMs=$($metrics.firstPartialTextOffsetMs) nativePayloadReadyMs=$($metrics.nativePayloadReadyOffsetMs) gatewayNativePayloadReadyMs=$($metrics.gatewayNativePayloadReadyOffsetMs)",
	"- WebView: clickToPreviewRequestMs=$($metrics.clickToPreviewRequestMs) clickToPreviewResponseMs=$($metrics.clickToPreviewResponseMs) clickToRenderMs=$($metrics.clickToRenderMs) previewResponseToRenderMs=$($metrics.previewResponseToRenderMs)",
	"- Steady-state partial interval estimate: $($metrics.steadyStatePartialIntervalMs) ms",
	"- Matched diagnostic lines: $($metrics.matchedLineCount)",
	"",
	"## Interpretation checklist",
	"",
	"- Use CUDA data only when effectiveProvider=cuda, available=true, enabled=true, and reason=active.",
	"- For CPU fallback tuning, require effectiveProvider=cpu and compare threads/mode/chunk settings by first-token latency.",
	"- Compare CPU and CUDA using the same utterances, chunk profile, model path, and app build.",
	"- Prefer the provider with lower click-to-render and first partial timing for the selected live-preview profile."
)
$markdown | Set-Content -LiteralPath $mdPath -Encoding UTF8

Write-Host "Benchmark JSON: $jsonPath"
Write-Host "Benchmark Markdown: $mdPath"
Write-Host "Effective provider: $($metrics.effectiveProvider)"
Write-Host "CUDA: available=$($metrics.cudaAvailable) enabled=$($metrics.cudaEnabled) reason=$($metrics.cudaReason)"
Write-Host "Matched diagnostic lines: $($metrics.matchedLineCount)"
