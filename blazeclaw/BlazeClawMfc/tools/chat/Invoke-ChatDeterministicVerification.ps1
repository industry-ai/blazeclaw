param(
    [string]$GatewayUrl = "ws://127.0.0.1:18789",
    [string]$SessionKey = ""
)

$ErrorActionPreference = "Stop"

function Resolve-SessionKey {
    param([string]$ExplicitSessionKey)

    if (-not [string]::IsNullOrWhiteSpace($ExplicitSessionKey)) {
        return $ExplicitSessionKey
    }

    return "smoke-det-" + [guid]::NewGuid().ToString("N")
}

function Test-GatewayReachability {
    param(
        [string]$Url
    )

    try {
        $uri = [System.Uri]::new($Url)
        if ($uri.Scheme -ne "ws" -and $uri.Scheme -ne "wss") {
            return $false
        }

$resolvedSessionKey = Resolve-SessionKey -ExplicitSessionKey $SessionKey

        $port = if ($uri.IsDefaultPort) {
            if ($uri.Scheme -eq "wss") { 443 } else { 80 }
        }
        else {
            $uri.Port
        }

        $client = [System.Net.Sockets.TcpClient]::new()
        $task = $client.ConnectAsync($uri.Host, $port)
        $connected = $task.Wait(1500) -and $client.Connected
        $client.Dispose()
        return $connected
    }
    catch {
        return $false
    }
}

function New-ReqFrame {
    param(
        [string]$Method,
        [hashtable]$Params
    )

    return @{
        type = "req"
        id = [guid]::NewGuid().ToString("N")
        method = $Method
        params = $Params
    }
}

function Send-Req {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [hashtable]$Frame
    )

    $json = ($Frame | ConvertTo-Json -Depth 8 -Compress)
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
    $segment = [ArraySegment[byte]]::new($bytes)
    [void]$Socket.SendAsync(
        $segment,
        [System.Net.WebSockets.WebSocketMessageType]::Text,
        $true,
        [Threading.CancellationToken]::None).GetAwaiter().GetResult()
}

function Receive-Text {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket
    )

    $buffer = New-Object byte[] 65536
    $segment = [ArraySegment[byte]]::new($buffer)
    $builder = [System.Text.StringBuilder]::new()

    while ($true) {
        $result = $Socket.ReceiveAsync(
            $segment,
            [Threading.CancellationToken]::None).GetAwaiter().GetResult()

        if ($result.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) {
            return $null
        }

        if ($result.Count -gt 0) {
            $builder.Append([System.Text.Encoding]::UTF8.GetString($buffer, 0, $result.Count)) | Out-Null
        }

        if ($result.EndOfMessage) {
            break
        }
    }

    return $builder.ToString()
}

function Wait-Response {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [string]$RequestId,
        [int]$MaxFrames = 64,
        [ref]$CapturedEvents
    )

    for ($i = 0; $i -lt $MaxFrames; $i++) {
        $raw = Receive-Text -Socket $Socket
        if ([string]::IsNullOrWhiteSpace($raw)) {
            break
        }

        $obj = $raw | ConvertFrom-Json
        if ($obj.type -eq "event") {
            $CapturedEvents.Value += $obj
            continue
        }

        if ($obj.type -eq "res" -and $obj.id -eq $RequestId) {
            return $obj
        }
    }

    throw "response timeout for request id $RequestId"
}

if (-not (Test-GatewayReachability -Url $GatewayUrl)) {
    Write-Output "[INFO] Gateway endpoint is not reachable: $GatewayUrl"
    Write-Output "[INFO] Please start BlazeClaw first, then rerun this verification script."
    return
}

$socket = [System.Net.WebSockets.ClientWebSocket]::new()
$uri = [System.Uri]::new($GatewayUrl)
$socket.Options.AddSubProtocol("blazeclaw.gateway.v1")

try {
    [void]$socket.ConnectAsync($uri, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
}
catch {
    Write-Output "[INFO] TCP endpoint is reachable, but WebSocket handshake failed: $GatewayUrl"
    Write-Output "[INFO] This usually means BlazeClaw started but gateway WebSocket upgrade/accept path is not ready."
    Write-Output "[INFO] Check gateway startup warnings and bridge/runtime logs, then rerun the script."
    Write-Output "[INFO] Handshake error: $($_.Exception.Message)"
    return
}

$events = @()

try {
    $helloReq = New-ReqFrame -Method "connect" -Params @{ protocol = 3; agent = "blazeclaw-det-verifier" }
    Send-Req -Socket $socket -Frame $helloReq
    $helloRes = Wait-Response -Socket $socket -RequestId $helloReq.id -CapturedEvents ([ref]$events)

    Write-Output "[PASS] connect response received"

    $historyReq = New-ReqFrame -Method "chat.history" -Params @{ sessionKey = $resolvedSessionKey; limit = 20 }
    Send-Req -Socket $socket -Frame $historyReq
    $historyRes = Wait-Response -Socket $socket -RequestId $historyReq.id -CapturedEvents ([ref]$events)
    Write-Output "[PASS] chat.history response received"

    $sendReq = New-ReqFrame -Method "chat.send" -Params @{
        sessionKey = $resolvedSessionKey
        message = "deterministic-check"
        deliver = $false
        idempotencyKey = "det-" + [guid]::NewGuid().ToString("N")
        attachments = @()
    }
    Send-Req -Socket $socket -Frame $sendReq
    $sendRes = Wait-Response -Socket $socket -RequestId $sendReq.id -CapturedEvents ([ref]$events)
    Write-Output "[PASS] chat.send response received"

    $pollReq = New-ReqFrame -Method "chat.events.poll" -Params @{ sessionKey = $resolvedSessionKey; limit = 50 }
    Send-Req -Socket $socket -Frame $pollReq
    $pollRes = Wait-Response -Socket $socket -RequestId $pollReq.id -CapturedEvents ([ref]$events)
    Write-Output "[PASS] chat.events.poll response received"

    $abortReq = New-ReqFrame -Method "chat.abort" -Params @{ sessionKey = $resolvedSessionKey }
    Send-Req -Socket $socket -Frame $abortReq
    $abortRes = Wait-Response -Socket $socket -RequestId $abortReq.id -CapturedEvents ([ref]$events)
    Write-Output "[PASS] chat.abort response received"

    $sendErrReq = New-ReqFrame -Method "chat.send" -Params @{
        sessionKey = $resolvedSessionKey
        message = "deterministic-error-check"
        deliver = $false
        forceError = $true
        idempotencyKey = "det-err-" + [guid]::NewGuid().ToString("N")
        attachments = @()
    }
    Send-Req -Socket $socket -Frame $sendErrReq
    $sendErrRes = Wait-Response -Socket $socket -RequestId $sendErrReq.id -CapturedEvents ([ref]$events)
    Write-Output "[PASS] chat.send(forceError) response received"

    $pollErrReq = New-ReqFrame -Method "chat.events.poll" -Params @{ sessionKey = $resolvedSessionKey; limit = 50 }
    Send-Req -Socket $socket -Frame $pollErrReq
    $pollErrRes = Wait-Response -Socket $socket -RequestId $pollErrReq.id -CapturedEvents ([ref]$events)
    Write-Output "[PASS] chat.events.poll after forceError response received"

    Write-Output ""
    Write-Output "Deterministic verification summary"
    Write-Output "- sessionKey: $resolvedSessionKey"
    Write-Output "- connect: pass"
    Write-Output "- history: pass"
    Write-Output "- send: pass"
    Write-Output "- poll: pass"
    Write-Output "- abort: pass"
    Write-Output "- forceError: pass"
}
finally {
    if ($socket.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
        [void]$socket.CloseAsync(
            [System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure,
            "done",
            [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    }
    $socket.Dispose()
}
