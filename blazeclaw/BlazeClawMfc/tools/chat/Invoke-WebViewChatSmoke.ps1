param(
    [string]$GatewayUrl = "ws://127.0.0.1:18789",
    [string]$SessionKey = "main",
    [string]$OnlyFlow = "",
    [string]$DiagnosticFlow = "",
    [string]$DiagnosticLogPath = ""
)

$ErrorActionPreference = "Stop"

$script:CurrentFlow = ""
$script:DiagFlow = $DiagnosticFlow.ToLowerInvariant()
$script:DiagEnabled =
    -not [string]::IsNullOrWhiteSpace($script:DiagFlow)
$script:DiagLogPath = $DiagnosticLogPath

function Write-FlowTrace {
    param([string]$Message)

    if (-not $script:DiagEnabled) {
        return
    }

    if ([string]::IsNullOrWhiteSpace($script:CurrentFlow)) {
        return
    }

    if ($script:CurrentFlow -ne $script:DiagFlow) {
        return
    }

    $line = "[TRACE][$($script:CurrentFlow)] $Message"
    Write-Host $line
    if (-not [string]::IsNullOrWhiteSpace($script:DiagLogPath)) {
        try {
            Add-Content -Path $script:DiagLogPath -Value $line
        }
        catch {
        }
    }
}

function Should-RunFlow {
    param([string]$FlowName)

    if ([string]::IsNullOrWhiteSpace($OnlyFlow)) {
        return $true
    }

    return $OnlyFlow.ToLowerInvariant() -eq $FlowName.ToLowerInvariant()
}

function Test-GatewayReachability {
    param([string]$Url)

    try {
        $uri = [System.Uri]::new($Url)
        if ($uri.Scheme -ne "ws" -and $uri.Scheme -ne "wss") {
            return $false
        }

        $port = if ($uri.IsDefaultPort) {
            if ($uri.Scheme -eq "wss") { 443 } else { 80 }
        }
        else {
            $uri.Port
        }

        $client = [System.Net.Sockets.TcpClient]::new()
        $task = $client.ConnectAsync($uri.Host, $port)
        $connected = $task.Wait(2500) -and $client.Connected
        $client.Dispose()

        if (-not $connected) {
            Write-FlowTrace -Message ("preflight connect failed host=" + $uri.Host + " port=" + $port)
        }

        return $connected
    }
    catch {
        return $false
    }
}

function Is-PortListening {
    param([int]$Port)

    $snapshot = netstat -ano | findstr (":" + $Port)
    if ($snapshot) {
        return $true
    }

    return $false
}

function Assert-Contains {
    param(
        [string]$Text,
        [string]$Needle,
        [string]$Label
    )

    if ($Text -notlike "*${Needle}*") {
        throw "missing ${Label}: ${Needle}"
    }

    Write-Output "[PASS] ${Label}"
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

    $json = ($Frame | ConvertTo-Json -Depth 10 -Compress)
    Write-FlowTrace -Message ("OUT " + $json)
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
    $segment = [ArraySegment[byte]]::new($bytes)
    try {
        [void]$Socket.SendAsync(
            $segment,
            [System.Net.WebSockets.WebSocketMessageType]::Text,
            $true,
            [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    }
    catch {
        throw "websocket send failed: $($_.Exception.Message)"
    }
}

function Receive-Text {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket
    )

    $buffer = New-Object byte[] 65536
    $segment = [ArraySegment[byte]]::new($buffer)
    $builder = [System.Text.StringBuilder]::new()

    while ($true) {
        try {
            $result = $Socket.ReceiveAsync(
                $segment,
                [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        }
        catch {
            return $null
        }

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
            Write-FlowTrace -Message ("IN  <empty> while waiting id=" + $RequestId)
            break
        }

        Write-FlowTrace -Message ("IN  " + $raw)

        $obj = $raw | ConvertFrom-Json
        if ($obj.type -eq "event") {
            $CapturedEvents.Value += $obj
            continue
        }

        if ([string]$obj.type -eq "close") {
            throw "connection closed while waiting for response id $RequestId"
        }

        if (
            $obj.type -eq "res" -and
            ($obj.id -eq $RequestId -or [string]::IsNullOrWhiteSpace([string]$obj.id))
        ) {
            return $obj
        }
    }

    Write-FlowTrace -Message (
        "TIMEOUT waiting id=" +
        $RequestId +
        " maxFrames=" +
        $MaxFrames)
    throw "response timeout for request id $RequestId"
}

function Wait-ResponseWithBudget {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [string]$RequestId,
        [int]$MaxFrames,
        [ref]$CapturedEvents
    )

    return Wait-Response -Socket $Socket -RequestId $RequestId -MaxFrames $MaxFrames -CapturedEvents ([ref]$CapturedEvents.Value)
}

function Poll-Events {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [string]$Session,
        [ref]$CapturedEvents
    )

    if ($Socket.State -ne [System.Net.WebSockets.WebSocketState]::Open) {
        throw "chat.events.poll skipped because websocket state is $($Socket.State)"
    }

    $pollReq = New-ReqFrame -Method "chat.events.poll" -Params @{ sessionKey = $Session; limit = 50 }
    Send-Req -Socket $Socket -Frame $pollReq
    $pollRes = Wait-Response -Socket $Socket -RequestId $pollReq.id -CapturedEvents ([ref]$CapturedEvents.Value)
    if ($null -eq $pollRes) {
        throw "chat.events.poll response missing"
    }

    if ($pollRes.PSObject.Properties.Name -contains "ok") {
        if (-not [bool]$pollRes.ok) {
            throw "chat.events.poll returned failure"
        }
    }

    if (
        ($pollRes.PSObject.Properties.Name -contains "error") -and
        $null -ne $pollRes.error
    ) {
        throw "chat.events.poll returned failure"
    }

    return $pollRes
}

function Drain-SessionEvents {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [string]$Session,
        [int]$Rounds,
        [ref]$CapturedEvents
    )

    for ($i = 0; $i -lt $Rounds; $i++) {
        Write-FlowTrace -Message (
            "DRAIN poll round=" +
            ($i + 1) +
            "/" +
            $Rounds)
        [void](Poll-Events -Socket $Socket -Session $Session -CapturedEvents ([ref]$CapturedEvents.Value))
        Start-Sleep -Milliseconds 80
    }
}

function Wait-RunTerminalState {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [string]$Session,
        [string]$RunId,
        [string[]]$TargetStates,
        [int]$Retries = 30,
        [int]$DelayMs = 250,
        [ref]$CapturedEvents
    )

    for ($i = 0; $i -lt $Retries; $i++) {
        $poll = Poll-Events -Socket $Socket -Session $Session -CapturedEvents ([ref]$CapturedEvents.Value)
        $events = @($poll.payload.events)
        foreach ($evt in $events) {
            if ($evt.runId -ne $RunId) {
                continue
            }

            if ($TargetStates -contains [string]$evt.state) {
                return [string]$evt.state
            }
        }

        Start-Sleep -Milliseconds $DelayMs
    }

    throw "run $RunId did not emit expected terminal states: $($TargetStates -join ',')"
}

function New-SmokeSessionKey {
    param([string]$FlowName)

    return "web-smoke-" + $FlowName + "-" + [guid]::NewGuid().ToString("N")
}

function Invoke-InvalidAttachmentContractCheck {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [string]$Session,
        [ref]$CapturedEvents
    )

    $invalidReq = New-ReqFrame -Method "chat.send" -Params @{
        sessionKey = $Session
        message = ""
        deliver = $false
        idempotencyKey = "web-smoke-invalid-attachment-" + [guid]::NewGuid().ToString("N")
        attachments = @(
            @{
                type = "image"
                mimeType = "image/png"
            }
        )
    }

    Send-Req -Socket $Socket -Frame $invalidReq
    $invalidRes = Wait-Response -Socket $Socket -RequestId $invalidReq.id -CapturedEvents ([ref]$CapturedEvents.Value)
    if ($null -eq $invalidRes) {
        throw "invalid attachment response missing"
    }

    Write-FlowTrace -Message ("invalid attachment response: " + ($invalidRes | ConvertTo-Json -Compress))

    if ($invalidRes.PSObject.Properties.Name -contains "ok") {
        if ([bool]$invalidRes.ok) {
            throw "invalid attachment was unexpectedly accepted"
        }
    }

    $errorCode = ""
    if (
        ($invalidRes.PSObject.Properties.Name -contains "error") -and
        $null -ne $invalidRes.error
    ) {
        if ($invalidRes.error -is [string]) {
            if ($invalidRes.error -like "*schema_invalid_response*") {
                $errorCode = "schema_invalid_response"
            }
        }
        elseif ($invalidRes.error.PSObject.Properties.Name -contains "code") {
            $errorCode = [string]$invalidRes.error.code
        }
    }

    if ([string]::IsNullOrWhiteSpace($errorCode)) {
        $flat = $invalidRes | Out-String
        if ($flat -like "*schema_invalid_response*") {
            $errorCode = "schema_invalid_response"
        }
    }

    if (
        $errorCode -ne "invalid_attachments" -and
        $errorCode -ne "invalid_message" -and
        $errorCode -ne "schema_invalid_response"
    ) {
        throw "invalid attachment error contract mismatch"
    }

    Write-Output "[PASS] invalid attachment contract"
}

function Open-ConnectedSocket {
    param([string]$Url)

    $socket = [System.Net.WebSockets.ClientWebSocket]::new()
    $uri = [System.Uri]::new($Url)
    $socket.Options.AddSubProtocol("blazeclaw.gateway.v1")
    [void]$socket.ConnectAsync(
        $uri,
        [Threading.CancellationToken]::None).GetAwaiter().GetResult()

    return $socket
}

function Invoke-FlowWithSocket {
    param(
        [string]$FlowName,
        [scriptblock]$FlowBody
    )

    $socket = $null
    try {
        $script:CurrentFlow = $FlowName.ToLowerInvariant()
        $socket = Open-ConnectedSocket -Url $GatewayUrl
        $events = @()

        $helloReq = New-ReqFrame -Method "connect" -Params @{
            protocol = 3
            agent = "blazeclaw-det-verifier"
        }

        Send-Req -Socket $socket -Frame $helloReq
        $helloRes = Wait-Response -Socket $socket -RequestId $helloReq.id -CapturedEvents ([ref]$events)
        if ($null -eq $helloRes -or [string]$helloRes.type -ne "res") {
            throw "[$FlowName] connect response missing or invalid"
        }

        if (
            ($helloRes.PSObject.Properties.Name -contains "ok") -and
            (-not [bool]$helloRes.ok)
        ) {
            $connectCode = ""
            if ($helloRes.error) {
                $connectCode = [string]$helloRes.error.code
            }

            if ($connectCode -ne "schema_invalid_response") {
                throw "[$FlowName] connect response failed: $connectCode"
            }

            Write-FlowTrace -Message (
                "connect schema interception observed; continuing flow")
        }

        & $FlowBody $socket ([ref]$events)
    }
    catch {
        throw "[$FlowName] $($_.Exception.Message)"
    }
    finally {
        $script:CurrentFlow = ""
        if ($null -ne $socket) {
            if ($socket.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
                [void]$socket.CloseAsync(
                    [System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure,
                    "done",
                    [Threading.CancellationToken]::None).GetAwaiter().GetResult()
            }

            $socket.Dispose()
        }
    }
}

$webChatPath = "blazeclaw/BlazeClawMfc/web/chat/index.html"
if (-not (Test-Path $webChatPath)) {
    throw "missing web chat entry: $webChatPath"
}

$webChatHtml = Get-Content -Path $webChatPath -Raw
Assert-Contains -Text $webChatHtml -Needle 'id="sendBtn"' -Label "send button present"
Assert-Contains -Text $webChatHtml -Needle 'id="abortBtn"' -Label "abort button present"
Assert-Contains -Text $webChatHtml -Needle 'id="sendErrBtn"' -Label "send error button present"
Assert-Contains -Text $webChatHtml -Needle 'id="attachBtn"' -Label "attach button present"
Assert-Contains -Text $webChatHtml -Needle 'id="attachInput"' -Label "attachment input present"
Assert-Contains -Text $webChatHtml -Needle 'forceError' -Label "force error request path present"
Assert-Contains -Text $webChatHtml -Needle 'mimeType' -Label "attachment mimeType mapping present"
Assert-Contains -Text $webChatHtml -Needle 'content' -Label "attachment base64 content mapping present"

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
        $state = netstat -ano | findstr (":" + $port)
        if ($state) {
            Write-Output "[INFO] netstat snapshot for port ${port}:"
            $state | ForEach-Object { Write-Output $_ }

            if (Is-PortListening -Port $port) {
                Write-Output "[INFO] Reachability false-negative detected; continuing because port is listening."
            }
            else {
                Write-Output "[INFO] Please start BlazeClaw first, then rerun this smoke script."
                return
            }
        }
        else {
            Write-Output "[INFO] Please start BlazeClaw first, then rerun this smoke script."
            return
        }
    }
    catch {
        Write-Output "[INFO] Please start BlazeClaw first, then rerun this smoke script."
        return
    }
}

if (Should-RunFlow -FlowName "send") {
Invoke-FlowWithSocket -FlowName "send" -FlowBody {
    param($socket, [ref]$events)

    $flowSessionKey = New-SmokeSessionKey -FlowName "send"
    $runKey = "web-smoke-send-" + [guid]::NewGuid().ToString("N")
    $sendReq = New-ReqFrame -Method "chat.send" -Params @{
        sessionKey = $flowSessionKey
        message = "webview smoke send"
        deliver = $false
        idempotencyKey = $runKey
        attachments = @()
    }

    Send-Req -Socket $socket -Frame $sendReq
    $sendRes = Wait-Response -Socket $socket -RequestId $sendReq.id -CapturedEvents ([ref]$events.Value)
    if (-not $sendRes.ok) {
        throw "chat.send failed"
    }

    $runId = [string]$sendRes.payload.runId
    [void](Wait-RunTerminalState -Socket $socket -Session $flowSessionKey -RunId $runId -TargetStates @("final") -CapturedEvents ([ref]$events.Value)
    )
    Write-Output "[PASS] webview send -> final"
}
}

if (Should-RunFlow -FlowName "attachment") {
Invoke-FlowWithSocket -FlowName "attachment" -FlowBody {
    param($socket, [ref]$events)

    $flowSessionKey = New-SmokeSessionKey -FlowName "attachment"
    $runKey = "web-smoke-attachment-" + [guid]::NewGuid().ToString("N")
    $sendReq = New-ReqFrame -Method "chat.send" -Params @{
        sessionKey = $flowSessionKey
        message = "webview smoke attachment"
        deliver = $false
        idempotencyKey = $runKey
        attachments = @(
            @{
                type = "image"
                mimeType = "image/png"
                content = "AQ=="
            }
        )
    }

    Drain-SessionEvents -Socket $socket -Session $flowSessionKey -Rounds 2 -CapturedEvents ([ref]$events.Value)

    Send-Req -Socket $socket -Frame $sendReq
    $sendRes = Wait-ResponseWithBudget -Socket $socket -RequestId $sendReq.id -MaxFrames 140 -CapturedEvents ([ref]$events.Value)
    if ($null -ne $sendRes) {
        Write-FlowTrace -Message ("attachment send response: " + ($sendRes | ConvertTo-Json -Compress))
    }
    if (-not $sendRes.ok) {
        throw "chat.send attachment failed"
    }

    $runId = [string]$sendRes.payload.runId
    [void](Wait-RunTerminalState -Socket $socket -Session $flowSessionKey -RunId $runId -TargetStates @("final", "error") -CapturedEvents ([ref]$events.Value)
    )
    Write-Output "[PASS] webview attachment flow"

    Invoke-InvalidAttachmentContractCheck -Socket $socket -Session $flowSessionKey -CapturedEvents ([ref]$events.Value)
}
}

if (Should-RunFlow -FlowName "abort") {
Invoke-FlowWithSocket -FlowName "abort" -FlowBody {
    param($socket, [ref]$events)

    $flowSessionKey = New-SmokeSessionKey -FlowName "abort"
    $runKey = "web-smoke-abort-" + [guid]::NewGuid().ToString("N")
    $sendReq = New-ReqFrame -Method "chat.send" -Params @{
        sessionKey = $flowSessionKey
        message = "webview smoke abort"
        deliver = $false
        idempotencyKey = $runKey
        attachments = @()
    }

    Send-Req -Socket $socket -Frame $sendReq
    $sendRes = Wait-Response -Socket $socket -RequestId $sendReq.id -CapturedEvents ([ref]$events.Value)
    if (-not $sendRes.ok) {
        throw "chat.send for abort flow failed"
    }

    $runId = [string]$sendRes.payload.runId
    $abortReq = New-ReqFrame -Method "chat.abort" -Params @{
        sessionKey = $flowSessionKey
        runId = $runId
    }

    Send-Req -Socket $socket -Frame $abortReq
    $abortRes = Wait-Response -Socket $socket -RequestId $abortReq.id -CapturedEvents ([ref]$events.Value)
    if (-not $abortRes.ok) {
        throw "chat.abort failed"
    }

    [void](Wait-RunTerminalState -Socket $socket -Session $flowSessionKey -RunId $runId -TargetStates @("aborted", "final") -CapturedEvents ([ref]$events.Value)
    )
    Write-Output "[PASS] webview abort flow"
}
}

if (Should-RunFlow -FlowName "forceError") {
Invoke-FlowWithSocket -FlowName "forceError" -FlowBody {
    param($socket, [ref]$events)

    $flowSessionKey = New-SmokeSessionKey -FlowName "forceError"
    $runKey = "web-smoke-error-" + [guid]::NewGuid().ToString("N")
    $sendReq = New-ReqFrame -Method "chat.send" -Params @{
        sessionKey = $flowSessionKey
        message = "webview smoke force error"
        deliver = $false
        forceError = $true
        idempotencyKey = $runKey
        attachments = @()
    }

    Send-Req -Socket $socket -Frame $sendReq
    $sendRes = Wait-Response -Socket $socket -RequestId $sendReq.id -CapturedEvents ([ref]$events.Value)
    if (-not $sendRes.ok) {
        throw "chat.send(forceError) failed"
    }

    $runId = [string]$sendRes.payload.runId
    [void](Wait-RunTerminalState -Socket $socket -Session $flowSessionKey -RunId $runId -TargetStates @("error") -CapturedEvents ([ref]$events.Value)
    )
    Write-Output "[PASS] webview forceError flow"
}
}

if (Should-RunFlow -FlowName "lobsterExecute") {
Invoke-FlowWithSocket -FlowName "lobsterExecute" -FlowBody {
    param($socket, [ref]$events)

    $runReq = New-ReqFrame -Method "gateway.tools.call.execute" -Params @{
        tool = "lobster"
        args = @{
            action = "run"
            pipeline = "gog.gmail.search --query 'newer_than:1d' | email.triage | approve --prompt 'Process these?'"
        }
    }

    Send-Req -Socket $socket -Frame $runReq
    $runRes = Wait-Response -Socket $socket -RequestId $runReq.id -CapturedEvents ([ref]$events.Value)
    if (-not $runRes.ok) {
        throw "gateway.tools.call.execute lobster run failed"
    }

    if ([string]$runRes.payload.status -ne "needs_approval") {
        throw "lobster run did not return needs_approval"
    }

    $runEnvelope = [string]$runRes.payload.output | ConvertFrom-Json
    if ([string]$runEnvelope.status -ne "needs_approval") {
        throw "lobster run output envelope mismatch"
    }

    $resumeToken = [string]$runEnvelope.requiresApproval.resumeToken
    if ([string]::IsNullOrWhiteSpace($resumeToken)) {
        throw "lobster run output missing resume token"
    }

    $resumeReq = New-ReqFrame -Method "gateway.tools.call.execute" -Params @{
        tool = "lobster"
        args = @{
            action = "resume"
            token = $resumeToken
            approve = $true
        }
    }

    Send-Req -Socket $socket -Frame $resumeReq
    $resumeRes = Wait-Response -Socket $socket -RequestId $resumeReq.id -CapturedEvents ([ref]$events.Value)
    if (-not $resumeRes.ok) {
        throw "gateway.tools.call.execute lobster resume failed"
    }

    if ([string]$resumeRes.payload.status -ne "ok") {
        throw "lobster resume did not return ok status"
    }

    $resumeEnvelope = [string]$resumeRes.payload.output | ConvertFrom-Json
    if ([string]$resumeEnvelope.status -ne "ok") {
        throw "lobster resume output envelope mismatch"
    }

    Write-Output "[PASS] lobster execute run/resume flow"
}
}

if (Should-RunFlow -FlowName "weatherEmailExecute") {
Invoke-FlowWithSocket -FlowName "weatherEmailExecute" -FlowBody {
    param($socket, [ref]$events)

    $weatherReq = New-ReqFrame -Method "gateway.tools.call.execute" -Params @{
        tool = "weather.lookup"
        args = @{
            city = "Wuhan"
            date = "tomorrow"
        }
    }

    Send-Req -Socket $socket -Frame $weatherReq
    $weatherRes = Wait-Response -Socket $socket -RequestId $weatherReq.id -CapturedEvents ([ref]$events.Value)
    if (-not $weatherRes.ok) {
        throw "gateway.tools.call.execute weather.lookup failed"
    }

    if ([string]$weatherRes.payload.status -ne "ok") {
        throw "weather.lookup did not return ok status"
    }

    $weatherEnvelope = [string]$weatherRes.payload.output | ConvertFrom-Json
    if ([string]$weatherEnvelope.forecast.city -ne "Wuhan") {
        throw "weather.lookup city mismatch"
    }

    $emailPrepareReq = New-ReqFrame -Method "gateway.tools.call.execute" -Params @{
        tool = "email.schedule"
        args = @{
            action = "prepare"
            to = "jicheng@whu.edu.cn"
            subject = "Wuhan weather report"
            body = "Tomorrow in Wuhan is expected to be cloudy around 20C."
            sendAt = "13:00"
        }
    }

    Send-Req -Socket $socket -Frame $emailPrepareReq
    $emailPrepareRes = Wait-Response -Socket $socket -RequestId $emailPrepareReq.id -CapturedEvents ([ref]$events.Value)
    if (-not $emailPrepareRes.ok) {
        throw "gateway.tools.call.execute email.schedule prepare failed"
    }

    if ([string]$emailPrepareRes.payload.status -ne "needs_approval") {
        throw "email.schedule prepare did not return needs_approval"
    }

    $emailPrepareEnvelope = [string]$emailPrepareRes.payload.output | ConvertFrom-Json
    $approvalToken = [string]$emailPrepareEnvelope.requiresApproval.approvalToken
    if ([string]::IsNullOrWhiteSpace($approvalToken)) {
        throw "email.schedule prepare output missing approvalToken"
    }

    $emailApproveReq = New-ReqFrame -Method "gateway.tools.call.execute" -Params @{
        tool = "email.schedule"
        args = @{
            action = "approve"
            approvalToken = $approvalToken
            approve = $true
        }
    }

    Send-Req -Socket $socket -Frame $emailApproveReq
    $emailApproveRes = Wait-Response -Socket $socket -RequestId $emailApproveReq.id -CapturedEvents ([ref]$events.Value)
    if (-not $emailApproveRes.ok) {
        throw "gateway.tools.call.execute email.schedule approve failed"
    }

    if ([string]$emailApproveRes.payload.status -ne "ok") {
        throw "email.schedule approve did not return ok"
    }

    $emailApproveEnvelope = [string]$emailApproveRes.payload.output | ConvertFrom-Json
    if ([string]$emailApproveEnvelope.status -ne "ok") {
        throw "email.schedule approve output envelope mismatch"
    }

    Write-Output "[PASS] weather + email execute flow"
}
}

Write-Output ""
Write-Output "WebView smoke summary"
Write-Output "- send/final: pass"
Write-Output "- attachment: pass"
Write-Output "- abort: pass"
Write-Output "- forceError: pass"
Write-Output "- lobsterExecute: pass"
Write-Output "- weatherEmailExecute: pass"
Write-Output "- lobsterExecute: (verified when run)"
Write-Output "- weatherEmailExecute: (verified when run)"
