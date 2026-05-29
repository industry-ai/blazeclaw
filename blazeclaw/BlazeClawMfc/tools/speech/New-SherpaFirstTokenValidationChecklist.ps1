param(
	[string] $OutputPath = "",

	[string] $BenchmarkSummaryDirectory = "",

	[string[]] $Scenarios = @(
		"cuda-active",
		"cpu-fallback",
		"first-utterance-after-startup",
		"second-utterance-warm-runtime",
		"chinese-short-utterance",
		"english-short-utterance"
	)
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

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
	$OutputPath = Join-Path $scriptRoot "sherpa-first-token-manual-validation-checklist.md"
}
if ([string]::IsNullOrWhiteSpace($BenchmarkSummaryDirectory)) {
	$BenchmarkSummaryDirectory = Join-Path $scriptRoot "first-token-validation-summaries"
}

$resolvedOutputPath = Resolve-FullPath $OutputPath
$resolvedSummaryDirectory = Resolve-FullPath $BenchmarkSummaryDirectory
New-Item -ItemType Directory -Path (Split-Path -Parent $resolvedOutputPath) -Force | Out-Null
New-Item -ItemType Directory -Path $resolvedSummaryDirectory -Force | Out-Null

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# Sherpa First-Token Manual Validation Checklist") | Out-Null
$lines.Add("") | Out-Null
$lines.Add("Use this checklist after changes that affect Sherpa Zipformer startup,") | Out-Null
$lines.Add("provider selection, streaming chunks, warmup, first-token timing, or") | Out-Null
$lines.Add("WebView live-preview rendering.") | Out-Null
$lines.Add("") | Out-Null
$lines.Add("## Shared setup") | Out-Null
$lines.Add("") | Out-Null
$lines.Add("- Build Debug x64 before manual validation.") | Out-Null
$lines.Add('- Use the active config file: `BlazeClawMfc/blazeclaw.conf`.') | Out-Null
$lines.Add("- Keep the Sherpa Zipformer model root unchanged during a comparison set.") | Out-Null
$lines.Add("- Capture startup log lines and WebView console diagnostics for each run.") | Out-Null
$lines.Add('- Summarize every captured log with `Invoke-SherpaProviderBenchmarkSummary.ps1`.') | Out-Null
$lines.Add('- Do not use CUDA benchmark data unless `effectiveProvider=cuda`,') | Out-Null
$lines.Add('  `available=true`, `enabled=true`, and `reason=active` are all present.') | Out-Null
$lines.Add("") | Out-Null
$lines.Add("## Required diagnostic fields") | Out-Null
$lines.Add("") | Out-Null
$lines.Add("Each summary should contain:") | Out-Null
$lines.Add("") | Out-Null
$lines.Add('- `effectiveProvider`') | Out-Null
$lines.Add("- CUDA availability, enabled state, and reason") | Out-Null
$lines.Add("- speech thread count and execution mode") | Out-Null
$lines.Add("- streaming latency profile and preview chunk/lookback") | Out-Null
$lines.Add("- model-load stage and latency") | Out-Null
$lines.Add("- warmup completion, success, provider, stage, and latency") | Out-Null
$lines.Add("- first encoder, decoder, joiner, and partial-text timings") | Out-Null
$lines.Add("- gateway native payload readiness") | Out-Null
$lines.Add("- WebView click-to-render and preview-response-to-render timings") | Out-Null
$lines.Add("- steady-state partial interval estimate when multiple preview updates exist") | Out-Null
$lines.Add("") | Out-Null
$lines.Add("## Scenario checklist") | Out-Null
$lines.Add("") | Out-Null

foreach ($scenario in $Scenarios) {
	$safeScenario = ($scenario -replace "[^A-Za-z0-9_.-]", "_")
	$logPath = Join-Path $resolvedSummaryDirectory "$safeScenario.log"
	$summaryCommand = "powershell -ExecutionPolicy Bypass -File `"$scriptRoot\Invoke-SherpaProviderBenchmarkSummary.ps1`" -LogPath `"$logPath`" -OutputDirectory `"$resolvedSummaryDirectory\$safeScenario`" -ProviderLabel `"$safeScenario`""
	if ($scenario -eq "cuda-active") {
		$summaryCommand += " -RequireCudaActive"
	}

	$lines.Add("### $scenario") | Out-Null
	$lines.Add("") | Out-Null
	$lines.Add("- [ ] Apply the intended provider/config settings for this scenario.") | Out-Null
	$lines.Add("- [ ] Relaunch BlazeClaw so startup diagnostics are fresh.") | Out-Null
	$lines.Add("- [ ] Record the target utterance or provider state for this scenario.") | Out-Null
	$lines.Add("- [ ] Save startup and WebView speech diagnostics to ``$logPath``.") | Out-Null
	$lines.Add("- [ ] Run summary command:") | Out-Null
	$lines.Add("") | Out-Null
	$lines.Add("  ``$summaryCommand``") | Out-Null
	$lines.Add("") | Out-Null
	$lines.Add("- [ ] Confirm required diagnostic fields are present.") | Out-Null
	$lines.Add("- [ ] Compare first partial and click-to-render latency against the previous") | Out-Null
	$lines.Add("      baseline for the same utterance and provider profile.") | Out-Null
	$lines.Add("") | Out-Null
}

$lines.Add("## Utterance guidance") | Out-Null
$lines.Add("") | Out-Null
$lines.Add('- Chinese short utterance: `讲一个笑话`') | Out-Null
$lines.Add("- English short utterance: use the same short phrase for every comparison set") | Out-Null
$lines.Add("  and record the phrase in the final validation notes.") | Out-Null
$lines.Add("- Run first-utterance and warm-runtime scenarios separately so startup/warmup") | Out-Null
$lines.Add("  latency does not hide streaming latency changes.") | Out-Null
$lines.Add("") | Out-Null
$lines.Add("## Pass criteria") | Out-Null
$lines.Add("") | Out-Null
$lines.Add("- Debug x64 build succeeds.") | Out-Null
$lines.Add("- Provider diagnostics identify CUDA active or the CPU fallback reason.") | Out-Null
$lines.Add("- First-token timing fields are present for live-preview runs.") | Out-Null
$lines.Add("- WebView render timing is present for live-preview runs.") | Out-Null
$lines.Add("- First-token latency can be compared before and after the optimization.") | Out-Null

$lines | Set-Content -LiteralPath $resolvedOutputPath -Encoding UTF8

Write-Host "Manual validation checklist: $resolvedOutputPath"
Write-Host "Benchmark summary directory: $resolvedSummaryDirectory"
