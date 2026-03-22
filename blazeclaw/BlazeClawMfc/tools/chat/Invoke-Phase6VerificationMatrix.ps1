param(
    [string]$GatewayUrl = "ws://127.0.0.1:18789",
    [string]$SessionKey = ""
)

$ErrorActionPreference = "Stop"

function Invoke-Step {
    param(
        [string]$Label,
        [scriptblock]$Action
    )

    Write-Output "[INFO] $Label"
    & $Action
    Write-Output "[PASS] $Label"
}

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$deterministicScript = Join-Path $scriptRoot "Invoke-ChatDeterministicVerification.ps1"
$webViewScript = Join-Path $scriptRoot "Invoke-WebViewChatSmoke.ps1"

if (-not (Test-Path $deterministicScript)) {
    throw "Missing script: $deterministicScript"
}

if (-not (Test-Path $webViewScript)) {
    throw "Missing script: $webViewScript"
}

Invoke-Step -Label "Deterministic gateway verification" -Action {
    & $deterministicScript -GatewayUrl $GatewayUrl -SessionKey $SessionKey
}

Invoke-Step -Label "WebView smoke verification" -Action {
    & $webViewScript -GatewayUrl $GatewayUrl -SessionKey "main"
}

Write-Output ""
Write-Output "Phase 6 verification matrix summary"
Write-Output "- deterministic verification: pass"
Write-Output "- webview smoke: pass"
Write-Output "- gateway url: $GatewayUrl"
