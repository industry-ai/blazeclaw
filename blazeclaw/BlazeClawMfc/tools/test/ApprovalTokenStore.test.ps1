param(
    [string]$StateDir = "$(Resolve-Path ..\..\..\..\blazeclaw\BlazeClawMfc)\state",
    [switch]$Verbose
)

$ErrorActionPreference = 'Stop'

function Write-Log { param($m) if ($Verbose) { Write-Host $m } }

if (-not (Test-Path $StateDir)) { New-Item -ItemType Directory -Path $StateDir | Out-Null }
$storePath = Join-Path $StateDir "approvals.json"

# Helper: create empty store
function Init-Store {
    param([string]$path)
    Set-Content -Path $path -Value '{}' -Encoding UTF8
}

function Read-Store {
    param([string]$path)
    return Get-Content -Path $path -Raw -ErrorAction Stop
}

function Save-Token {
    param($path, $token, $payloadJson)
    $content = Read-Store -path $path
    $obj = $null
    try { $obj = $content | ConvertFrom-Json -ErrorAction Stop } catch { $obj = @{} }
    $obj.$token = ($payloadJson | ConvertFrom-Json -ErrorAction Stop)
    $obj | ConvertTo-Json -Depth 10 | Set-Content -Path $path -Encoding UTF8
}

function Load-Token {
    param($path, $token)
    $content = Read-Store -path $path
    $obj = $content | ConvertFrom-Json
    if ($obj.PSObject.Properties.Name -contains $token) {
        return $obj.$token | ConvertTo-Json -Depth 10 -Compress
    }
    return $null
}

function Remove-Token {
    param($path, $token)
    $content = Read-Store -path $path
    $obj = $content | ConvertFrom-Json
    if ($obj.PSObject.Properties.Name -contains $token) {
        $obj.PSObject.Properties.Remove($token) | Out-Null
        $obj | ConvertTo-Json -Depth 10 | Set-Content -Path $path -Encoding UTF8
        return $true
    }
    return $false
}

# Begin tests
Init-Store -path $storePath
Write-Log "Store initialized at $storePath"

$token = "test-token-123"
$payload = '{"to":"alice@example.com","subject":"Hi","body":"Hello","sendAt":"10:00"}'

Save-Token -path $storePath -token $token -payloadJson $payload
Write-Log "Saved token $token"

$loaded = Load-Token -path $storePath -token $token
if ([string]::IsNullOrWhiteSpace($loaded)) { throw "Load-Token failed" }
Write-Host "[PASS] Load-Token returned payload for $token"

# Validate payload fields
$payloadObj = $loaded | ConvertFrom-Json
if ($payloadObj.to -ne 'alice@example.com') { throw "payload 'to' mismatch" }
if ($payloadObj.subject -ne 'Hi') { throw "payload 'subject' mismatch" }
Write-Host "[PASS] payload fields match"

# Remove token
$removed = Remove-Token -path $storePath -token $token
if (-not $removed) { throw "Remove-Token failed" }
Write-Host "[PASS] Remove-Token removed $token"

$loaded2 = Load-Token -path $storePath -token $token
if ($loaded2 -ne $null) { throw "token still present after remove" }
Write-Host "[PASS] token not present after remove"

Write-Host "ApprovalTokenStore PS-unit tests completed successfully"
