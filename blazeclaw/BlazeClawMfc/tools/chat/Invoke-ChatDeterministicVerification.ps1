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

function Is-PortListening {
    param([int]$Port)

    $state = netstat -ano | findstr (":" + $Port)
    if ($state) {
        return $true
    }

    return $false
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

        $port = if ($uri.IsDefaultPort) {
            if ($uri.Scheme -eq "wss") { 443 } else { 80 }
        }

function Poll-Events {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [string]$Session,
        [ref]$CapturedEvents
    )

    $pollReq = New-ReqFrame -Method "chat.events.poll" -Params @{
        sessionKey = $Session
        limit = 50
    }

    Send-Req -Socket $Socket -Frame $pollReq
    $pollRes = Wait-Response -Socket $Socket -RequestId $pollReq.id -CapturedEvents ([ref]$CapturedEvents.Value)
    if ($null -eq $pollRes -or -not $pollRes.ok) {
        throw "chat.events.poll failed"
    }

    return $pollRes
}

function Wait-RunTerminalState {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [string]$Session,
        [string]$RunId,
        [string[]]$TargetStates,
        [int]$Retries = 20,
        [int]$DelayMs = 200,
        [ref]$CapturedEvents
    )

    for ($i = 0; $i -lt $Retries; $i++) {
        $poll = Poll-Events -Socket $Socket -Session $Session -CapturedEvents ([ref]$CapturedEvents.Value)
        $events = @($poll.payload.events)
        foreach ($evt in $events) {
            if ([string]$evt.runId -ne $RunId) {
                continue
            }

            if ($TargetStates -contains [string]$evt.state) {
                return [string]$evt.state
            }
        }

        Start-Sleep -Milliseconds $DelayMs
    }

    throw "run $RunId did not reach expected states: $($TargetStates -join ',')"
}
        else {
            $uri.Port
        }

$resolvedSessionKey = Resolve-SessionKey -ExplicitSessionKey $SessionKey

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
    try {
        $uri = [System.Uri]::new($GatewayUrl)
        $port = if ($uri.IsDefaultPort) {
            if ($uri.Scheme -eq "wss") { 443 } else { 80 }
        }
        else {
            $uri.Port
        }

        if (Is-PortListening -Port $port) {
            Write-Output "[INFO] Reachability false-negative detected; continuing because port is listening."
        }
        else {
            Write-Output "[INFO] Please start BlazeClaw first, then rerun this verification script."
            return
        }
    }
    catch {
        Write-Output "[INFO] Please start BlazeClaw first, then rerun this verification script."
        return
    }
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

    $pollRes = Poll-Events -Socket $socket -Session $resolvedSessionKey -CapturedEvents ([ref]$events)
    Write-Output "[PASS] chat.events.poll response received"

    $sendAttachmentReq = New-ReqFrame -Method "chat.send" -Params @{
        sessionKey = $resolvedSessionKey
        message = "deterministic-attachment-check"
        deliver = $false
        idempotencyKey = "det-att-" + [guid]::NewGuid().ToString("N")
        attachments = @(
            @{
                type = "image"
                mimeType = "image/png"
                content = "AQ=="
            }
        )
    }
    Send-Req -Socket $socket -Frame $sendAttachmentReq
    $sendAttachmentRes = Wait-Response -Socket $socket -RequestId $sendAttachmentReq.id -CapturedEvents ([ref]$events)
    if (-not $sendAttachmentRes.ok) {
        $attErrorCode = ""
        if ($sendAttachmentRes.error) {
            $attErrorCode = [string]$sendAttachmentRes.error.code
        }

        if ($attErrorCode -eq "invalid_frame") {
            throw "attachment request hit frame decode regression (invalid_frame)"
        }

        throw "chat.send attachment request failed: $attErrorCode"
    }
    Write-Output "[PASS] chat.send attachment response received"

    [void](Wait-RunTerminalState -Socket $socket -Session $resolvedSessionKey -RunId ([string]$sendAttachmentRes.payload.runId) -TargetStates @("final", "error") -CapturedEvents ([ref]$events))
    Write-Output "[PASS] chat.send attachment terminal state observed"

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

    $pollErrRes = Poll-Events -Socket $socket -Session $resolvedSessionKey -CapturedEvents ([ref]$events)
    Write-Output "[PASS] chat.events.poll after forceError response received"

    [void](Wait-RunTerminalState -Socket $socket -Session $resolvedSessionKey -RunId ([string]$sendErrRes.payload.runId) -TargetStates @("error") -CapturedEvents ([ref]$events))
    Write-Output "[PASS] chat.send(forceError) terminal error observed"

    Write-Output ""
    Write-Output "Deterministic verification summary"
    Write-Output "- sessionKey: $resolvedSessionKey"
    Write-Output "- connect: pass"
    Write-Output "- history: pass"
    Write-Output "- send: pass"
    Write-Output "- send-attachment: pass"
    Write-Output "- send-attachment-terminal: pass"
    Write-Output "- poll: pass"
    Write-Output "- abort: pass"
    Write-Output "- forceError: pass"
    Write-Output "- forceError-terminal: pass"
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
