param(
    [string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
}

$chatRoot = Join-Path $RepoRoot "BlazeClawMfc\web\chat"
$indexHtml = Join-Path $chatRoot "index.html"
$indexJs = Join-Path $chatRoot "index.js"
$agentsToggleJs = Join-Path $chatRoot "agents-toggle.js"
$regressionRunner = Join-Path $PSScriptRoot "run-agents-toggle-regression.js"
$confPath = Join-Path $RepoRoot "BlazeClawMfc\blazeclaw.conf"

function Assert-FileContains {
    param(
        [string]$Path,
        [string]$Pattern,
        [string]$Label
    )

    if (-not (Test-Path $Path)) {
        throw "Missing file for gate '$Label': $Path"
    }

    $content = Get-Content -Path $Path -Raw
    if ($content -notmatch $Pattern) {
        throw "Gate failed '$Label' in $Path (pattern: $Pattern)"
    }

    Write-Host "[cron-cli-gate] pass: $Label"
}

Write-Host "[cron-cli-gate] repoRoot=$RepoRoot"

Assert-FileContains $indexHtml 'agents-toggle\.js' "index.html includes agents-toggle.js"
Assert-FileContains $indexJs 'cronCliEnabled' "index.js wires cronCliEnabled"
Assert-FileContains $indexJs 'control_plane_disabled' "index.js exposes control_plane_disabled code"
Assert-FileContains $indexJs 'agents_controller_missing' "index.js exposes agents_controller_missing code"
Assert-FileContains $indexJs '__BLAZECLAW_RESYNC_AGENTS_CONTROL_PLANE__' "index.js exposes resync hook"
Assert-FileContains $agentsToggleJs 'blazeclaw\.agents\.toggle\.trace' "agents-toggle forwards trace to native channel"

$availabilityHeader = Join-Path $RepoRoot "BlazeClawMfc\src\app\WebView2Availability.h"
$bridgeCpp = Join-Path $RepoRoot "BlazeClawMfc\src\app\WebViewStartupConfigBridge.cpp"
$bridgeSupportCpp = Join-Path $RepoRoot "BlazeClawMfc\src\app\WebViewBridgeSupport.cpp"

if (-not (Test-Path $availabilityHeader)) {
    throw "Missing shared WebView2 availability header: $availabilityHeader"
}
Write-Host "[cron-cli-gate] pass: WebView2Availability.h exists"

Assert-FileContains $availabilityHeader '#\s*define\s+HAVE_WEBVIEW2_HEADER' "WebView2Availability.h defines HAVE_WEBVIEW2_HEADER when WebView2 is available"
Assert-FileContains $bridgeCpp 'WebView2Availability\.h' "WebViewStartupConfigBridge.cpp includes WebView2Availability.h"
Assert-FileContains $bridgeSupportCpp 'WebView2Availability\.h' "WebViewBridgeSupport.cpp includes WebView2Availability.h"
Assert-FileContains $bridgeCpp 'IsWebViewStartupBridgeCompiled' "WebViewStartupConfigBridge exposes compile guard"

if (Test-Path $confPath) {
    Assert-FileContains $confPath 'blazeclaw\.agents\.enabled\s*=' "blazeclaw.conf documents agents.enabled key"
}

Write-Host "[cron-cli-gate] running agents-toggle JS regression..."
& node $regressionRunner
if ($LASTEXITCODE -ne 0) {
    throw "agents-toggle JS regression failed with exit code $LASTEXITCODE"
}

Write-Host "[cron-cli-gate] all automated gates passed."
Write-Host "[cron-cli-gate] manual E2E checklist:"
Write-Host "  1) Set blazeclaw.agents.enabled=1 in blazeclaw.conf (only toggle source)."
Write-Host "  2) Clear WebView localStorage blazeclaw.agents.enabled if present."
Write-Host "  3) Start BlazeClaw chat WebView; confirm debug lines:"
Write-Host "     [Chat] startup.agents.controlPlane - enabled=true source=blazeclaw.conf"
Write-Host "     [Chat] startup.webview.runtimeConfig.agents - enabled=true"
Write-Host "     [Chat] startup.webview.agentsToggle - resolved=true source=config ..."
Write-Host "  4) Type /cron status; response must NOT contain code=unavailable."
Write-Host "  5) Regression: /help, /clear, /model still work."
