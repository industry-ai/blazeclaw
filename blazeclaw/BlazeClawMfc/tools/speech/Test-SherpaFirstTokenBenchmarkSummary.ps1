param(
	[Parameter(Mandatory = $true)]
	[string] $SummaryJsonPath,

	[switch] $RequireCudaActive,

	[switch] $RequireCpuFallback,

	[switch] $RequireFirstTokenTiming,

	[switch] $RequireWebViewTiming
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-FullPath {
	param([string] $PathValue)

	$expanded = [Environment]::ExpandEnvironmentVariables($PathValue)
	if ([System.IO.Path]::IsPathRooted($expanded)) {
		return [System.IO.Path]::GetFullPath($expanded)
	}

	return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $expanded))
}

function Assert-HasValue {
	param(
		[object] $Object,
		[string] $Name
	)

	if (-not ($Object.PSObject.Properties.Name -contains $Name)) {
		throw "Missing benchmark summary field: $Name"
	}

	$value = $Object.$Name
	if ($null -eq $value) {
		throw "Benchmark summary field is null: $Name"
	}

	if ($value -is [string] -and [string]::IsNullOrWhiteSpace($value)) {
		throw "Benchmark summary field is empty: $Name"
	}
}

function Assert-AnyHasValue {
	param(
		[object] $Object,
		[string[]] $Names,
		[string] $Label
	)

	foreach ($name in $Names) {
		if ($Object.PSObject.Properties.Name -contains $name) {
			$value = $Object.PSObject.Properties[$name].Value
			if ($null -ne $value -and
				(-not ($value -is [string]) -or -not [string]::IsNullOrWhiteSpace($value))) {
				return
			}
		}
	}

	throw "Missing required benchmark summary group: $Label"
}

$resolvedSummaryJsonPath = Resolve-FullPath $SummaryJsonPath
if (-not (Test-Path -LiteralPath $resolvedSummaryJsonPath -PathType Leaf)) {
	throw "Summary JSON not found: $resolvedSummaryJsonPath"
}

$summary = Get-Content -LiteralPath $resolvedSummaryJsonPath -Encoding UTF8 -Raw |
	ConvertFrom-Json

Assert-HasValue $summary "effectiveProvider"
Assert-HasValue $summary "matchedLineCount"
Assert-AnyHasValue $summary @(
	"cudaAvailable",
	"cudaEnabled",
	"cudaReason"
) "CUDA availability/enabled/reason"
Assert-AnyHasValue $summary @(
	"streamingLatencyProfile",
	"streamingPreviewChunkMs",
	"streamingPreviewLookbackMs"
) "streaming profile/chunk/lookback"
Assert-AnyHasValue $summary @(
	"modelLoadStage",
	"modelLoadLatencyMs"
) "model-load diagnostics"
Assert-AnyHasValue $summary @(
	"warmupCompleted",
	"warmupSucceeded",
	"warmupProvider",
	"warmupStage",
	"warmupLatencyMs"
) "warmup diagnostics"

if ($RequireFirstTokenTiming) {
	Assert-AnyHasValue $summary @(
		"firstEncoderStartOffsetMs",
		"firstEncoderEndOffsetMs",
		"firstDecoderStartOffsetMs",
		"firstJoinerStartOffsetMs",
		"firstPartialTextOffsetMs",
		"nativePayloadReadyOffsetMs",
		"gatewayNativePayloadReadyOffsetMs"
	) "first-token timing"
}

if ($RequireWebViewTiming) {
	Assert-AnyHasValue $summary @(
		"clickToPreviewRequestMs",
		"clickToPreviewResponseMs",
		"clickToRenderMs",
		"previewResponseToRenderMs"
	) "WebView timing"
}

if ($RequireCudaActive) {
	if ($summary.effectiveProvider -ne "cuda" -or
		$summary.cudaAvailable -ne $true -or
		$summary.cudaEnabled -ne $true -or
		$summary.cudaReason -ne "active") {
		throw "CUDA active summary requirement failed."
	}
}

if ($RequireCpuFallback) {
	if ($summary.effectiveProvider -ne "cpu") {
		throw "CPU fallback summary requirement failed: effectiveProvider=$($summary.effectiveProvider)"
	}

	Assert-AnyHasValue $summary @(
		"cudaReason",
		"speechThreads",
		"speechExecutionMode"
	) "CPU fallback reason/thread/mode diagnostics"
}

Write-Host "Sherpa first-token benchmark summary validation passed: $resolvedSummaryJsonPath"
