/**
 * [AgentChat 文件说明]
 * Node 聊天桥接：将前端请求转发到上游 TLS 聊天 API（生产形态）。
 */
/**
 * Bridges browser HTTP → TLS:9443 chat (AppProtoHeader 64B + JSON payload).
 * Matches chatAPI.md: IrcMessageReq=221, IrcMessageResp=222.
 *
 * Header layout matches AppProtoHeader(1).h:
 *   bytes[4] magic        @0  = "HBPC"
 *   u8       version      @4  = 1
 *   u8       type         @5
 *   u8       flags        @6
 *   u8       reserved1    @7
 *   u32      seq          @8  (big-endian)
 *   u32      payload_len  @12 (big-endian)
 *   u64      session_id   @16 (big-endian)
 *   u64      timestamp_ms @24 (big-endian)
 *   bytes[16] auth_token  @32
 *   bytes[16] reserved    @48
 *
 * Env:
 *   CHAT_TLS_HOST (default 192.168.20.211)
 *   CHAT_TLS_PORT (default 9443)
 *   CHAT_BRIDGE_PORT (default 8787)
 *   CHAT_TLS_TIMEOUT_MS (default 30000) — max wait for connect + full 222 response
 *   CHAT_TLS_REJECT_UNAUTHORIZED=0 — dev only, self-signed server cert
 *   NODE_TLS_REJECT_UNAUTHORIZED=0 — fallback (same behavior as above)
 *   CHAT_TLS_PREFLIGHT_ENABLE=1 — send one preflight frame after connect
 *   CHAT_TLS_PREFLIGHT_REQ_TYPE=9 — preflight request MsgType (e.g. Session_verify)
 *   CHAT_TLS_PREFLIGHT_RESP_TYPE=9 — expected preflight response MsgType
 *   CHAT_TLS_PREFLIGHT_PAYLOAD='{}' — preflight JSON payload (UTF-8)
 */

import http from 'node:http'
import net from 'node:net'
import tls from 'node:tls'
import { URL } from 'node:url'

const MsgType = Object.freeze({
  Unknown: 0,
  ShutdownRequest: 5,
  ShutdownConfirm: 6,
  ShutdownWithdraw: 7,
  Session_failed: 8,
  Session_verify: 9,
  ConnectTcp: 10,
  ConnectTls: 11,
  Test: 18,
  Ping: 21,
  Pong: 22,
  Data: 23,
  Auth: 24,
  TYPE_AUTH_REQ: 101,
  TYPE_AUTH_RESP: 102,
  TYPE_OTP_VERIFY_REQ: 103,
  TYPE_OTP_VERIFY_RESP: 104,
  OtpRequest: 105,
  OtpResponse: 106,
  TYPE_OTP_SEND_REQ: 115,
  TYPE_OTP_SEND_RESP: 116,
  AuthRequest: 151,
  AuthResponse: 152,
  LoginSms: 153,
  LoginSmsResponse: 154,
  PhoneOtpSend: 155,
  PhoneOtpVerify: 156,
  PhoneOtpResponse: 157,
  SupabaseQuery: 201,
  SupabaseInsert: 202,
  SupabaseDelete: 203,
  SupabaseUpdate: 204,
  SupabaseResult: 205,
  CosUpload: 211,
  CosDownload: 212,
  CosDelete: 213,
  CosList: 214,
  CosResult: 215,
  IrcMessageReq: 221,
  IrcMessageResp: 222,
  CommandReq: 230,
  CommandResp: 231,
})
const HEADER_SIZE = 64
const PROTO_VERSION = 1
const MAX_PAYLOAD_SIZE = 64 * 1024
const PROTO_MAGIC = Buffer.from('HBPC', 'ascii')

const TCP_MODE = (process.env.CHAT_TCP_MODE ?? 'plain').trim().toLowerCase()
const USE_TLS = TCP_MODE === 'tls'
const HOST = process.env.CHAT_TCP_HOST ?? process.env.CHAT_TLS_HOST ?? '101.132.254.212'
const PORT = Number(process.env.CHAT_TCP_PORT ?? process.env.CHAT_TLS_PORT ?? '8765')
const BRIDGE = Number(process.env.CHAT_BRIDGE_PORT ?? '8787')
const TLS_TIMEOUT_MS = Number(process.env.CHAT_TLS_TIMEOUT_MS ?? '30000')
const SESSION_ID_WORD_SWAP =
  String(process.env.CHAT_SESSION_ID_WORD_SWAP ?? '0').trim().toLowerCase() === '1'
const rejectUnauthorizedRaw =
  process.env.CHAT_TLS_REJECT_UNAUTHORIZED ?? process.env.NODE_TLS_REJECT_UNAUTHORIZED ?? '1'
const REJECT_UNAUTHORIZED = !['0', 'false', 'no', 'off'].includes(
  String(rejectUnauthorizedRaw).trim().toLowerCase(),
)
const PREFLIGHT_ENABLE =
  String(process.env.CHAT_TLS_PREFLIGHT_ENABLE ?? '0').trim().toLowerCase() === '1'
const PREFLIGHT_REQ_TYPE = Number(
  process.env.CHAT_TLS_PREFLIGHT_REQ_TYPE ?? MsgType.Session_verify,
)
const PREFLIGHT_RESP_TYPE = Number(
  process.env.CHAT_TLS_PREFLIGHT_RESP_TYPE ?? MsgType.Session_verify,
)
const PREFLIGHT_PAYLOAD_TEXT = process.env.CHAT_TLS_PREFLIGHT_PAYLOAD ?? '{}'

let seq = 0
/** @type {(net.Socket | tls.TLSSocket) | null} */
let sock = null
let connecting = null
let preflightDone = false

/** Auth server connection (separate from chat server) */
let authSock = null
let authConnecting = null
const AUTH_HOST = process.env.AUTH_HOST || '139.224.189.70'
const AUTH_PORT = Number(process.env.AUTH_PORT || 9443)

/** Serialize framed requests on one TLS socket (one in-flight read/write at a time). */
let sendChain = Promise.resolve()
/**
 * 对齐 Vue 版 NativeTcpJoinState：记录每个 session 已 JOIN 的频道及 JOIN ACK 时间。
 * key: `${sessionId}:${channel}`, value: joinedAt(ms)。
 * 用于 JOIN refresh before send（>30s 重新 JOIN）和 heartbeat（每 25s 重新 JOIN）。
 */
const joinedBySession = new Map()
/** 对齐 Vue 版 JOIN_REFRESH_BEFORE_SEND_MS：超过 30s 重新 JOIN */
const JOIN_REFRESH_BEFORE_SEND_MS = 30_000
/** 对齐 Vue 版 HEARTBEAT_INTERVAL_MS：每 25s 重新 JOIN，防止服务端空闲超时(≈60s)丢弃 JOIN 状态 */
const HEARTBEAT_INTERVAL_MS = 25_000
let heartbeatTimer = null
/** 断线重连后需要重新 JOIN 的频道列表 */
let pendingRejoin = []
/**
 * Accumulates channel IDs discovered via LIST_CHANNELS events.
 * Keyed by sessionId, each value is a Set of channel IDs.
 * Used by GET /conversations to return the user's channel list.
 */
const discoveredChannels = new Map()

// ── 后台帧读取器：持续读取 TCP socket 的 222 推送事件 ──
// 对齐 Vue 版 NativeTcpChatTransportClient 的 onFrame 持续读取机制。
// 服务端会异步推送 PRIVMSG 广播、GROUP_INVITED 等事件到已连接的客户端，
// 桥接层必须持续读取才能捕获这些推送事件，转发给前端。
/** @type {Map<number, { resolve: Function, reject: Function, timer: NodeJS.Timeout }>} */
const pendingCommands = new Map()
/** 按 sessionId 分组的推送事件队列（key: sessionId, value: array） */
const pushEventQueueBySession = new Map()
/** 已知 session 集合：所有曾向桥接发起请求的 session_id */
const knownSessions = new Set()
let bgReaderActive = false
let bgReaderSocket = null

function _trackSession(sessionId) {
  const sid = String(sessionId || '').trim()
  if (sid && sid !== '0') knownSessions.add(sid)
}

function _getSessionQueue(sessionId) {
  let q = pushEventQueueBySession.get(sessionId)
  if (!q) {
    q = []
    pushEventQueueBySession.set(sessionId, q)
  }
  return q
}

function _drainSessionQueue(sessionId) {
  const q = pushEventQueueBySession.get(sessionId)
  if (!q || q.length === 0) return []
  const events = q.splice(0, q.length)
  return events
}

function _enqueuePushEvent(payload) {
  if (!payload || typeof payload !== 'object') return
  const evt = {
    ...payload,
    _received_at: Date.now(),
  }
  const eventName = String(payload.event ?? '').toUpperCase()
  const channel = String(payload.channel ?? payload.room_id ?? '').trim()
  const normalizedChannel = channel && !channel.startsWith('#') ? `#${channel}` : channel

  if (eventName === 'GROUP_INVITED') {
    // GROUP_INVITED：服务端推送给被邀请方。
    // 共享连接下无法精确确定被邀请方，投递给所有已知 session，
    // 前端会根据自身是否已有该会话来过滤。
    for (const sid of knownSessions) {
      const q = _getSessionQueue(sid)
      q.push(evt)
      if (q.length > 200) q.shift()
    }
  } else if (eventName === 'PRIVMSG' || eventName === 'ONLINE' || eventName === 'OFFLINE' || eventName === 'AGENT_TYPING') {
    // 对齐 Vue 版 NativeTcpChatTransportClient：服务端已按 JOIN 状态路由推送，
    // 桥接层不再按本地 joinedBySession 二次过滤，避免 JOIN 状态不同步时丢消息。
    // 投递给所有已知 session，前端按会话归属自行过滤。
    for (const sid of knownSessions) {
      const q = _getSessionQueue(sid)
      q.push(evt)
      if (q.length > 200) q.shift()
    }
  } else {
    // 其他事件：投递给所有已知 session
    for (const sid of knownSessions) {
      const q = _getSessionQueue(sid)
      q.push(evt)
      if (q.length > 200) q.shift()
    }
  }
}

function startBackgroundReader(s) {
  if (bgReaderActive && bgReaderSocket === s) return
  if (bgReaderSocket && bgReaderSocket !== s) {
    bgReaderActive = false
  }
  bgReaderActive = true
  bgReaderSocket = s
  console.log('[chat-bridge] Background frame reader started')

  const readLoop = async () => {
    while (bgReaderActive && s === bgReaderSocket && !s.destroyed && s.readyState === 'open') {
      try {
        const { header, body } = await readFrame(s)
        _lastSocketUse = Date.now()

        const text = body.toString('utf8')
        let parsed
        try {
          parsed = JSON.parse(text)
          parsed = normalizeServerPayload(parsed)
        } catch {
          parsed = { raw: text, parseError: true }
        }

        // 检查是否是命令响应（通过 seq 匹配）
        const pending = pendingCommands.get(header.seq)
        if (pending) {
          pendingCommands.delete(header.seq)
          clearTimeout(pending.timer)
          pending.resolve(parsed)
        } else {
          // 推送事件 — 加入队列
          _enqueuePushEvent(parsed)
          if (process.env.CHAT_DEBUG_PUSH ?? '0' === '1') {
            console.log('[chat-bridge] Push event queued:', JSON.stringify(parsed).slice(0, 200))
          }
        }
      } catch (e) {
        if (bgReaderActive && s === bgReaderSocket) {
          if (_isSocketError(e)) {
            console.warn('[chat-bridge] Background reader socket error:', e.message)
            forceReconnect()
          } else {
            console.warn('[chat-bridge] Background reader error:', e.message)
          }
        }
        break
      }
    }
    bgReaderActive = false
    console.log('[chat-bridge] Background frame reader stopped')
  }

  readLoop().catch(() => {
    bgReaderActive = false
  })
}

function stopBackgroundReader() {
  bgReaderActive = false
  bgReaderSocket = null
}

function rejectAllPendingCommands(error) {
  for (const [_seq, pending] of pendingCommands) {
    clearTimeout(pending.timer)
    pending.reject(error)
  }
  pendingCommands.clear()
}

function withTlsLock(fn) {
  const run = sendChain.then(() => fn())
  sendChain = run.catch(() => {})
  return run
}

function forceReconnect() {
  stopBackgroundReader()
  stopHeartbeat()
  rejectAllPendingCommands(new Error('connection reset'))
  // 对齐 Vue 版 clearForDisconnect：保存已 JOIN 频道，重连后重新 JOIN
  pendingRejoin = [...joinedBySession.keys()]
  try {
    sock?.destroy()
  } catch {
    // ignore
  }
  sock = null
  connecting = null
  preflightDone = false
  joinedBySession.clear()
}

// ── Heartbeat：对齐 Vue 版 startHeartbeat ──
// 每 25s 重新 JOIN 所有频道，防止服务端空闲超时(≈60s)丢弃 JOIN 状态。
function startHeartbeat() {
  if (heartbeatTimer) return // 已在运行，不重置定时器
  heartbeatTimer = setInterval(() => {
    _rejoinAllChannels().catch(() => {})
  }, HEARTBEAT_INTERVAL_MS)
}

function stopHeartbeat() {
  if (heartbeatTimer) {
    clearInterval(heartbeatTimer)
    heartbeatTimer = null
  }
}

/** 重新 JOIN 所有已加入的频道（仅 JOIN 超过 25s 的） */
async function _rejoinAllChannels() {
  if (joinedBySession.size === 0) return
  const entries = [...joinedBySession.entries()]
  for (const [joinKey, joinedAt] of entries) {
    if (Date.now() - joinedAt < HEARTBEAT_INTERVAL_MS) continue
    const colonIdx = joinKey.indexOf(':')
    if (colonIdx < 0) continue
    const sessionId = joinKey.slice(0, colonIdx)
    const channel = joinKey.slice(colonIdx + 1)
    if (!sessionId || !channel) continue
    try {
      const resp = await withTlsLock(() =>
        withTimeout(
          sendIrcCommand({ cmd: 'JOIN', channel, ts: Math.floor(Date.now() / 1000) }, sessionId),
          TLS_TIMEOUT_MS,
          'heartbeat JOIN timeout',
        ),
      )
      if (resp && typeof resp === 'object' && resp.event !== 'ERROR') {
        joinedBySession.set(joinKey, Date.now())
      }
    } catch {
      // heartbeat failure is silent — read-loop error handler owns reconnect
    }
  }
}

/** 断线重连后重新 JOIN 所有 pendingRejoin 频道 */
async function _rejoinPendingChannels() {
  if (pendingRejoin.length === 0) return
  const channels = pendingRejoin
  pendingRejoin = []
  for (const joinKey of channels) {
    const colonIdx = joinKey.indexOf(':')
    if (colonIdx < 0) continue
    const sessionId = joinKey.slice(0, colonIdx)
    const channel = joinKey.slice(colonIdx + 1)
    if (!sessionId || !channel) continue
    try {
      const resp = await withTlsLock(() =>
        withTimeout(
          sendIrcCommand({ cmd: 'JOIN', channel, ts: Math.floor(Date.now() / 1000) }, sessionId),
          TLS_TIMEOUT_MS,
          'reconnect JOIN timeout',
        ),
      )
      if (resp && typeof resp === 'object' && resp.event !== 'ERROR') {
        joinedBySession.set(joinKey, Date.now())
      }
    } catch {
      // re-join failure is silent, will retry on next command or heartbeat
    }
  }
}

/** 判断错误是否为 socket 层面故障，需要强制重连 */
function _isSocketError(error) {
  const msg = error instanceof Error ? error.message : String(error || '')
  return (
    /ECONNABORTED|ECONNRESET|EPIPE|ECONNREFUSED|socket closed before full frame|ended by the other party/i.test(msg) ||
    error?.code === 'ECONNABORTED' ||
    error?.code === 'ECONNRESET' ||
    error?.code === 'EPIPE' ||
    error?.code === 'ECONNREFUSED'
  )
}

function withTimeout(promise, ms, message) {
  return new Promise((resolve, reject) => {
    const t = setTimeout(() => reject(new Error(message)), ms)
    promise.then(
      (v) => {
        clearTimeout(t)
        resolve(v)
      },
      (e) => {
        clearTimeout(t)
        reject(e)
      },
    )
  })
}

function buildHeader(type, payloadByteLength, sessionId = 0n, timestampMs = 0n, noSwap = false) {
  const buf = Buffer.alloc(HEADER_SIZE, 0)
  PROTO_MAGIC.copy(buf, 0)
  buf.writeUInt8(PROTO_VERSION, 4)
  buf.writeUInt8(type & 0xff, 5)
  buf.writeUInt8(0, 6) // flags
  buf.writeUInt8(0, 7) // reserved1
  buf.writeUInt32BE(++seq >>> 0, 8)
  buf.writeUInt32BE(payloadByteLength >>> 0, 12)
  const sid = BigInt(sessionId)
  const wireSid = noSwap ? sid : toWireSessionId(sid)
  buf.writeBigUInt64BE(wireSid, 16)
  buf.writeBigUInt64BE(BigInt(timestampMs), 24)
  if (String(process.env.CHAT_DEBUG_HEADER ?? '0').trim() === '1') {
    const sidHex = buf.subarray(16, 24).toString('hex')
    // eslint-disable-next-line no-console
    console.log(
      `[bridge-header] seq=${buf.readUInt32BE(8)} payload_len=${buf.readUInt32BE(12)} session_id=${sid.toString()} wire_session_id=${wireSid.toString()} sid_hex=${sidHex}`,
    )
  }
  return buf
}

function swapSessionIdWords(sid) {
  const hi = (sid >> 32n) & 0xffffffffn
  const lo = sid & 0xffffffffn
  return (lo << 32n) | hi
}

function toWireSessionId(sid) {
  return SESSION_ID_WORD_SWAP ? swapSessionIdWords(sid) : sid
}

function fromWireSessionId(sid) {
  return SESSION_ID_WORD_SWAP ? swapSessionIdWords(sid) : sid
}

function parseHeader(buf) {
  if (buf.length < HEADER_SIZE) return null
  return {
    magic: buf.subarray(0, 4).toString('ascii'),
    version: buf.readUInt8(4),
    type: buf.readUInt8(5),
    flags: buf.readUInt8(6),
    reserved1: buf.readUInt8(7),
    seq: buf.readUInt32BE(8),
    payloadLen: buf.readUInt32BE(12),
    sessionId: buf.readBigUInt64BE(16),
    timestampMs: buf.readBigUInt64BE(24),
  }
}

function readExactly(stream, size) {
  return new Promise((resolve, reject) => {
    const chunks = []
    let total = 0

    const onError = (e) => {
      cleanup()
      reject(e)
    }

    const onEnd = () => {
      cleanup()
      reject(new Error('TLS socket closed before full frame'))
    }

    function cleanup() {
      stream.off('error', onError)
      stream.off('end', onEnd)
      stream.off('readable', tryRead)
    }

    function tryRead() {
      let chunk
      while ((chunk = stream.read(size - total)) !== null) {
        chunks.push(chunk)
        total += chunk.length
        if (total >= size) {
          cleanup()
          resolve(Buffer.concat(chunks, size))
          return
        }
      }
    }

    stream.on('error', onError)
    stream.on('end', onEnd)
    stream.on('readable', tryRead)
    tryRead()
  })
}

let _lastSocketUse = 0

async function getSocket() {
  // 超过 60s 未使用则断开重连，防止过期连接池导致协议错乱
  // 同时检查 readyState：poll 读取失败后 socket 可能处于半关闭状态（'end' 已触发但 'close' 未触发），
  // 此时 !sock.destroyed 仍为 true 但 write 会 ECONNABORTED
  if (sock && !sock.destroyed && sock.readyState === 'open' && Date.now() - _lastSocketUse < 60000) {
    _lastSocketUse = Date.now()
    return sock
  }
  if (sock && !sock.destroyed) {
    // Socket 被销毁时保存 JOIN 状态，新连接建立后由 _rejoinPendingChannels 重新 JOIN。
    // 对齐 forceReconnect 的 pendingRejoin 机制，避免服务端丢失 JOIN 状态后收不到推送。
    pendingRejoin = [...new Set([...pendingRejoin, ...joinedBySession.keys()])]
    sock.destroy()
    sock = null
  }
  if (connecting) return connecting

  connecting = new Promise((resolve, reject) => {
    const onConnected = (s) => {
      connecting = null
      sock = s
      _lastSocketUse = Date.now()
      s.on('error', () => {
        sock = null
      })
      s.on('close', () => {
        sock = null
      })
      resolve(s)
    }

    if (USE_TLS) {
      const s = tls.connect(
        {
          host: HOST,
          port: PORT,
          rejectUnauthorized: REJECT_UNAUTHORIZED,
        },
        () => onConnected(s),
      )
      s.once('error', (e) => {
        connecting = null
        reject(e)
      })
      return
    }

    const s = net.connect({ host: HOST, port: PORT }, () => onConnected(s))
    s.once('error', (e) => {
      connecting = null
      reject(e)
    })
  })
  return connecting
}

/** Auth server TLS connection */
async function getAuthSocket() {
  if (authSock && !authSock.destroyed) return authSock
  if (authConnecting) return authConnecting

  authConnecting = new Promise((resolve, reject) => {
    const onConnected = (s) => {
      authConnecting = null
      authSock = s
      s.on('error', () => { authSock = null })
      s.on('close', () => { authSock = null })
      resolve(s)
    }

    const s = tls.connect(
      { host: AUTH_HOST, port: AUTH_PORT, servername: 'zptls.blazegraph.site', rejectUnauthorized: false },
      () => onConnected(s),
    )

    s.once('error', (e) => {
      authConnecting = null
      reject(e)
    })
  })
  return authConnecting
}

/** Send a command through the auth TLS socket using IrcMessageReq/IrcMessageResp protocol */
async function sendAuthIrcCommand(command, sessionId) {
  const payload = Buffer.from(JSON.stringify(command), 'utf8')
  if (payload.length > MAX_PAYLOAD_SIZE) throw new Error('auth payload too large')
  const normalizedSessionId =
    typeof sessionId === 'string' || typeof sessionId === 'number' || typeof sessionId === 'bigint'
      ? BigInt(sessionId || 0)
      : 0n

  const s = await getAuthSocket()
  const header = buildHeader(MsgType.IrcMessageReq, payload.length, normalizedSessionId)

  await new Promise((resolve, reject) => {
    s.write(Buffer.concat([header, payload]), (err) => (err ? reject(err) : resolve()))
  })

  const { header: h, body } = await readFrame(s)
  if (h.type !== MsgType.IrcMessageResp) {
    throw new Error(`unexpected auth response type=${h?.type}, expected ${MsgType.IrcMessageResp}`)
  }
  const text = body.toString('utf8')
  try { return JSON.parse(text) }
  catch { return { raw: text } }
}

async function readFrame(stream) {
  const hdrBuf = await readExactly(stream, HEADER_SIZE)
  const h = parseHeader(hdrBuf)
  if (!h || h.magic !== 'HBPC') {
    throw new Error(`invalid response magic=${h?.magic ?? 'null'}`)
  }
  if (h.version !== PROTO_VERSION) {
    throw new Error(`unsupported response version=${h.version}`)
  }
  if (h.payloadLen > MAX_PAYLOAD_SIZE) {
    throw new Error('invalid payload_len')
  }
  const body = h.payloadLen ? await readExactly(stream, h.payloadLen) : Buffer.alloc(0)
  return { header: h, body }
}

async function ensurePreflight(s) {
  if (!USE_TLS || !PREFLIGHT_ENABLE || preflightDone) return
  let preflightPayload
  try {
    preflightPayload = Buffer.from(PREFLIGHT_PAYLOAD_TEXT, 'utf8')
  } catch {
    throw new Error('invalid CHAT_TLS_PREFLIGHT_PAYLOAD encoding')
  }
  if (preflightPayload.length > MAX_PAYLOAD_SIZE) {
    throw new Error('preflight payload too large')
  }

  const reqHeader = buildHeader(PREFLIGHT_REQ_TYPE, preflightPayload.length)
  await new Promise((resolve, reject) => {
    s.write(
      Buffer.concat([reqHeader, preflightPayload]),
      (err) => (err ? reject(err) : resolve()),
    )
  })

  const { header, body } = await readFrame(s)
  if (header.type !== PREFLIGHT_RESP_TYPE) {
    throw new Error(
      `preflight response type mismatch: expected=${PREFLIGHT_RESP_TYPE}, actual=${header.type}`,
    )
  }
  preflightDone = true
  // eslint-disable-next-line no-console
  console.log(
    `[chat-bridge] preflight ok (reqType=${PREFLIGHT_REQ_TYPE}, respType=${header.type}, payloadLen=${body.length})`,
  )
}

/**
 * 独立连接专用 preflight：每次都执行，不检查全局 preflightDone。
 * 用于 sendAppProtoCommandStandalone，确保新 TCP 连接完成 TLS 握手协商。
 */
async function ensurePreflightOnSocket(s) {
  if (!USE_TLS || !PREFLIGHT_ENABLE) return
  let preflightPayload
  try {
    preflightPayload = Buffer.from(PREFLIGHT_PAYLOAD_TEXT, 'utf8')
  } catch {
    throw new Error('invalid CHAT_TLS_PREFLIGHT_PAYLOAD encoding')
  }
  if (preflightPayload.length > MAX_PAYLOAD_SIZE) {
    throw new Error('preflight payload too large')
  }

  const reqHeader = buildHeader(PREFLIGHT_REQ_TYPE, preflightPayload.length)
  await new Promise((resolve, reject) => {
    s.write(
      Buffer.concat([reqHeader, preflightPayload]),
      (err) => (err ? reject(err) : resolve()),
    )
  })

  const { header, body } = await readFrame(s)
  if (header.type !== PREFLIGHT_RESP_TYPE) {
    throw new Error(
      `preflight response type mismatch: expected=${PREFLIGHT_RESP_TYPE}, actual=${header.type}`,
    )
  }
}

async function sendIrcCommand(command, sessionId) {
  const payload = Buffer.from(JSON.stringify(command), 'utf8')
  if (payload.length > MAX_PAYLOAD_SIZE) {
    throw new Error('payload too large')
  }
  const normalizedSessionId =
    typeof sessionId === 'string' || typeof sessionId === 'number' || typeof sessionId === 'bigint'
      ? BigInt(sessionId || 0)
      : 0n

  try {
    const s = await getSocket()
    await ensurePreflight(s)

    // 启动后台帧读取器（持续读取服务端推送的 222 帧）
    startBackgroundReader(s)

    // 对齐 Vue 版：连接建立后启动 heartbeat 并重新 JOIN 断线前的频道
    startHeartbeat()
    void _rejoinPendingChannels()

    // 构建命令头（buildHeader 内部递增全局 seq）
    const header = buildHeader(MsgType.IrcMessageReq, payload.length, normalizedSessionId)
    const cmdSeq = header.readUInt32BE(8)

    // 注册 pending command，等待后台读取器按 seq 匹配响应
    const responsePromise = new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        if (pendingCommands.has(cmdSeq)) {
          pendingCommands.delete(cmdSeq)
          reject(new Error(`sendIrcCommand timeout: seq=${cmdSeq}, cmd=${command.cmd}`))
        }
      }, TLS_TIMEOUT_MS)
      pendingCommands.set(cmdSeq, { resolve, reject, timer })
    })

    // 发送命令到 socket
    await new Promise((resolve, reject) => {
      s.write(Buffer.concat([header, payload]), (err) => (err ? reject(err) : resolve()))
    })

    // 等待后台读取器返回匹配的响应
    const resp = await responsePromise

    // NOT_JOINED 时清理 joinedBySession 缓存，下次 PRIVMSG 会重新 JOIN
    if (resp && typeof resp === 'object' && resp.event === 'ERROR' && resp.code === 'NOT_JOINED') {
      const channel = command.channel
      if (channel) {
        const joinKey = `${sessionId}:${channel}`
        joinedBySession.delete(joinKey)
      }
    }
    return resp
  } catch (error) {
    if (_isSocketError(error)) {
      forceReconnect()
    }
    throw error
  }
}

/**
 * Send an AppProto command using CommandReq(230)/CommandResp(231) HBPC types.
 * Aligns with Vue's sendAppProtoCommand → AuthBridge.nodeBindRequest.
 * Used for room management: CREATE_CONVERSATION, LIST_MY_CHAT_ROOMS, GET_ROOM_INFO, etc.
 */
async function sendAppProtoCommand(command, body, sessionId) {
  const cmdBody = { cmd: command, ...body, ts: body.ts || Math.floor(Date.now() / 1000) }
  const payload = Buffer.from(JSON.stringify(cmdBody), 'utf8')
  if (payload.length > MAX_PAYLOAD_SIZE) {
    throw new Error('appproto payload too large')
  }
  const normalizedSessionId =
    typeof sessionId === 'string' || typeof sessionId === 'number' || typeof sessionId === 'bigint'
      ? BigInt(sessionId || 0)
      : 0n

  try {
    const s = await getSocket()
    await ensurePreflight(s)

    const header = buildHeader(MsgType.CommandReq, payload.length, normalizedSessionId)

    await new Promise((resolve, reject) => {
      s.write(Buffer.concat([header, payload]), (err) => (err ? reject(err) : resolve()))
    })

    const { header: h, body } = await readFrame(s)
    if (h.type !== MsgType.CommandResp) {
      throw new Error(`unexpected appproto response type=${h?.type}, expected ${MsgType.CommandResp}`)
    }
    const text = body.toString('utf8')
    try {
      const parsed = JSON.parse(text)
      return normalizeServerPayload(parsed)
    } catch {
      return { raw: text, parseError: true }
    }
  } catch (error) {
    throw error
  }
}

function normalizeServerPayload(payload) {
  if (!SESSION_ID_WORD_SWAP || !payload || typeof payload !== 'object') {
    return payload
  }
  // Backend compatibility mode: convert response session_id back to canonical value.
  if ('session_id' in payload) {
    const raw = payload.session_id
    const asBigInt =
      typeof raw === 'string' || typeof raw === 'number' || typeof raw === 'bigint'
        ? BigInt(raw)
        : null
    if (asBigInt != null) {
      payload.session_id = fromWireSessionId(asBigInt).toString()
    }
  }
  return payload
}

/**
 * 独立连接版 sendAppProtoCommand：每次创建专用 TCP 连接，不复用全局 socket。
 * 对齐 Vue 版 AuthBridge.nodeBindRequest 的行为（每次独立连接），
 * 避免与 221 poll 共用 socket 时残留帧串扰（230 请求读到 222 响应）。
 */
async function sendAppProtoCommandStandalone(command, body, sessionId) {
  const cmdBody = { cmd: command, ...body, ts: body.ts || Math.floor(Date.now() / 1000) }
  const payload = Buffer.from(JSON.stringify(cmdBody), 'utf8')
  if (payload.length > MAX_PAYLOAD_SIZE) {
    throw new Error('appproto payload too large')
  }
  const normalizedSessionId =
    typeof sessionId === 'string' || typeof sessionId === 'number' || typeof sessionId === 'bigint'
      ? BigInt(sessionId || 0)
      : 0n

  let s = null
  try {
    s = await new Promise((resolve, reject) => {
      const onConn = (socket) => resolve(socket)
      if (USE_TLS) {
        const socket = tls.connect(
          { host: HOST, port: PORT, rejectUnauthorized: REJECT_UNAUTHORIZED },
          () => onConn(socket),
        )
        socket.once('error', reject)
      } else {
        const socket = net.connect({ host: HOST, port: PORT }, () => onConn(socket))
        socket.once('error', reject)
      }
    })

    // 独立连接必须做自己的 preflight，不复用全局 preflightDone
    await ensurePreflightOnSocket(s)

    // 230 CommandReq 期望原始 session_id（不做 WORD_SWAP）。
    // 与 221 IrcMessageReq 不同：221 通道需要 WORD_SWAP，230 通道需要原始值。
    // 对齐 Vue 版 AuthBridge.nodeBindRequest 的行为（原生直发原始 session_id）。
    const header = buildHeader(MsgType.CommandReq, payload.length, normalizedSessionId, 0n, true)
    await new Promise((resolve, reject) => {
      s.write(Buffer.concat([header, payload]), (err) => (err ? reject(err) : resolve()))
    })

    const { header: h, body: respBody } = await readFrame(s)
    // 服务端对 CommandReq(230) 可能返回 CommandResp(231) 或 IrcMessageResp(222)，
    // 两种都接受，只要能解析 JSON 即可。
    if (h.type !== MsgType.CommandResp && h.type !== MsgType.IrcMessageResp) {
      throw new Error(`unexpected appproto response type=${h?.type}, expected ${MsgType.CommandResp} or ${MsgType.IrcMessageResp}`)
    }
    const text = respBody.toString('utf8')
    try {
      const parsed = JSON.parse(text)
      return normalizeServerPayload(parsed)
    } catch {
      return { raw: text, parseError: true }
    }
  } finally {
    try { s?.destroy() } catch { /* ignore */ }
  }
}

/**
 * 独立连接版 sendIrcCommand：每次创建专用 TCP 连接（221/222 协议），不复用全局 socket。
 * 用于 CREATE_CONVERSATION 等低频但关键命令，避免与 poll 共用 socket 时 ECONNABORTED。
 */
async function sendIrcCommandStandalone(command, sessionId) {
  const payload = Buffer.from(JSON.stringify(command), 'utf8')
  if (payload.length > MAX_PAYLOAD_SIZE) {
    throw new Error('payload too large')
  }
  const normalizedSessionId =
    typeof sessionId === 'string' || typeof sessionId === 'number' || typeof sessionId === 'bigint'
      ? BigInt(sessionId || 0)
      : 0n

  let s = null
  try {
    s = await new Promise((resolve, reject) => {
      const onConn = (socket) => resolve(socket)
      if (USE_TLS) {
        const socket = tls.connect(
          { host: HOST, port: PORT, rejectUnauthorized: REJECT_UNAUTHORIZED },
          () => onConn(socket),
        )
        socket.once('error', reject)
      } else {
        const socket = net.connect({ host: HOST, port: PORT }, () => onConn(socket))
        socket.once('error', reject)
      }
    })

    await ensurePreflight(s)

    const header = buildHeader(MsgType.IrcMessageReq, payload.length, normalizedSessionId)
    await new Promise((resolve, reject) => {
      s.write(Buffer.concat([header, payload]), (err) => (err ? reject(err) : resolve()))
    })

    const { header: h, body: respBody } = await readFrame(s)
    if (h.type !== MsgType.IrcMessageResp) {
      throw new Error(`unexpected response header type=${h?.type}`)
    }
    const text = respBody.toString('utf8')
    try {
      const parsed = JSON.parse(text)
      return normalizeServerPayload(parsed)
    } catch {
      return { raw: text, parseError: true }
    }
  } finally {
    try { s?.destroy() } catch { /* ignore */ }
  }
}

function normalizeClientTsToSeconds(ts) {
  if (typeof ts !== 'number' || !Number.isFinite(ts) || ts <= 0) return null
  // Accept both seconds and milliseconds as per docs.
  return ts > 4_000_000_000 ? Math.floor(ts / 1000) : Math.floor(ts)
}

async function sendIrcPrivmsg(channel, message, sessionId, tsSeconds = null) {
  const ts = tsSeconds ?? Math.floor(Date.now() / 1000)
  const privmsg = {
    cmd: 'PRIVMSG',
    channel,
    message,
    ts,
  }
  const joinKey = `${sessionId}:${channel}`
  // 对齐 Vue 版 shouldRefreshJoinBeforeSend：超过 30s 重新 JOIN
  const joinedAt = joinedBySession.get(joinKey)
  const needsJoin = !joinedAt || Date.now() - joinedAt >= JOIN_REFRESH_BEFORE_SEND_MS
  if (needsJoin) {
    const joinResp = await sendIrcCommand(
      {
        cmd: 'JOIN',
        channel,
        ts,
      },
      sessionId,
    )
    if (joinResp && typeof joinResp === 'object' && joinResp.event === 'ERROR') {
      return joinResp
    }
    joinedBySession.set(joinKey, Date.now())
  }
  const resp = await sendIrcCommand(privmsg, sessionId)
  // 对齐 Vue 版 NOT_JOINED 处理：清理 join 状态，重新 JOIN 后重试一次
  if (resp && typeof resp === 'object' && resp.event === 'ERROR' && resp.code === 'NOT_JOINED') {
    joinedBySession.delete(joinKey)
    const rejoinResp = await sendIrcCommand(
      {
        cmd: 'JOIN',
        channel,
        ts,
      },
      sessionId,
    )
    if (rejoinResp && typeof rejoinResp === 'object' && rejoinResp.event === 'ERROR') {
      return rejoinResp
    }
    joinedBySession.set(joinKey, Date.now())
    return sendIrcCommand(privmsg, sessionId)
  }
  return resp
}

function json(res, code, obj) {
  if (res.writableEnded) return
  const body = JSON.stringify(obj)
  res.writeHead(code, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body, 'utf8'),
    Connection: 'close',
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Headers': 'Content-Type, sessionid',
    'Access-Control-Allow-Methods': 'GET,POST,PUT,DELETE,OPTIONS',
  })
  res.end(body)
}

function isRecoverableBridgeQueryError(message) {
  const text = String(message || '').toLowerCase()
  return (
    text.includes('econnaborted') ||
    text.includes('socket closed before full frame') ||
    text.includes('socket has been ended by the other party') ||
    text.includes('missing_channel_or_message')
  )
}

function readJsonBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = []
    req.on('data', (c) => chunks.push(c))
    req.on('end', () => {
      try {
        const raw = Buffer.concat(chunks).toString('utf8')
        resolve(raw ? JSON.parse(raw) : {})
      } catch (e) {
        reject(e)
      }
    })
    req.on('error', reject)
  })
}

async function handleRequest(req, res) {
  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      Connection: 'close',
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Headers': 'Content-Type, sessionid',
      'Access-Control-Allow-Methods': 'GET,POST,PUT,DELETE,OPTIONS',
    })
    res.end()
    return
  }

  const url = new URL(req.url ?? '/', `http://${req.headers.host}`)

  if (req.method === 'GET' && url.pathname === '/health') {
    // 验证 TCP 后端可达性，避免假阳性导致客户端误判连接状态
    try {
      await withTlsLock(() =>
        withTimeout(
          getSocket().then(s => { /* socket obtained, backend reachable */ }),
          5000,
          'TCP backend unreachable',
        ),
      )
      json(res, 200, { ok: true, tcpMode: USE_TLS ? 'tls' : 'plain', target: `${HOST}:${PORT}` })
    } catch (e) {
      json(res, 503, { ok: false, error: `TCP backend unreachable: ${e.message}` })
    }
    return
  }

  if (req.method === 'POST' && url.pathname === '/send') {
    try {
      const body = await readJsonBody(req)
      const cmdRaw = typeof body.cmd === 'string' ? body.cmd.trim() : ''
      const cmd = (cmdRaw || 'PRIVMSG').toUpperCase()
      const text = typeof body.message === 'string' ? body.message : ''
      const sessionIdRaw = body.session_id ?? body.sessionId
      const sessionId =
        typeof sessionIdRaw === 'string' || typeof sessionIdRaw === 'number' || typeof sessionIdRaw === 'bigint'
          ? String(sessionIdRaw).trim()
          : ''
      let channel = typeof body.channel === 'string' ? body.channel.trim() : ''
      const conv = typeof body.conversationId === 'string' ? body.conversationId.trim() : ''
      if (!channel && conv) {
        channel = conv.startsWith('#') ? conv : `#${conv}`
      }
      const tsSeconds = normalizeClientTsToSeconds(body.ts)

      if (!channel || !sessionId) {
        json(res, 400, { error: 'missing_channel_or_session_id' })
        return
      }

      // 追踪 session，用于推送事件路由
      _trackSession(sessionId)

      const resp = await withTlsLock(() =>
        withTimeout(
          cmd === 'JOIN'
            ? sendIrcCommand({ cmd: 'JOIN', channel, ts: tsSeconds ?? Math.floor(Date.now() / 1000) }, sessionId)
            : cmd === 'LEAVE'
              ? sendIrcCommand({ cmd: 'LEAVE', channel, ts: tsSeconds ?? Math.floor(Date.now() / 1000) }, sessionId)
              : cmd === 'PRIVMSG'
                ? (() => {
                    if (!text) {
                      const err = new Error('missing_message')
                      // @ts-ignore
                      err.statusCode = 400
                      throw err
                    }
                    return sendIrcPrivmsg(channel, text, sessionId, tsSeconds)
                  })()
                : sendIrcCommand(
                    { cmd, channel, message: text, ts: tsSeconds ?? Math.floor(Date.now() / 1000), ...(Array.isArray(body.attachments) ? { attachments: body.attachments } : {}) },
                    sessionId,
                  ),
          TLS_TIMEOUT_MS,
          `TLS timeout after ${TLS_TIMEOUT_MS}ms — no full IrcMessageResp (type=${MsgType.IrcMessageResp}), or connect hung. Check ${HOST}:${PORT} and C++ logs.`,
        ).catch((e) => {
          if (e instanceof Error && e.message.startsWith('TLS timeout')) {
            forceReconnect()
          }
          throw e
        }),
      )

      // JOIN 成功后记录到 joinedBySession，使后台帧读取器能将房间内广播
      // （PRIVMSG / ONLINE / OFFLINE / AGENT_TYPING）路由到该 session 的事件队列。
      // 不记录的话，其他成员发消息时该 session 收不到推送。
      if (cmd === 'JOIN' && resp && typeof resp === 'object' && resp.event !== 'ERROR') {
        const joinKey = `${sessionId}:${channel}`
        joinedBySession.set(joinKey, Date.now())
      }
      // LEAVE 时清除 joinedBySession，停止向该 session 投递该房间的推送
      if (cmd === 'LEAVE') {
        const joinKey = `${sessionId}:${channel}`
        joinedBySession.delete(joinKey)
      }

      json(res, 200, { ok: true, server: resp })
    } catch (e) {
      // Allow our own 400 errors.
      const status = typeof e === 'object' && e && 'statusCode' in e ? Number(e.statusCode) : 502
      const msg = e instanceof Error ? e.message : String(e)
      json(res, status === 400 ? 400 : 502, { ok: false, error: msg })
    }
    return
  }

  if (req.method === 'POST' && url.pathname === '/poll') {
    try {
      const body = await readJsonBody(req)
      const sessionIdRaw = body.session_id ?? body.sessionId
      const sessionId =
        typeof sessionIdRaw === 'string' || typeof sessionIdRaw === 'number' || typeof sessionIdRaw === 'bigint'
          ? String(sessionIdRaw).trim()
          : ''
      const timeoutMs = Math.min(Number(body.timeout_ms) || 25000, 30000)

      if (!sessionId) {
        json(res, 400, { error: 'missing_session_id' })
        return
      }

      // 追踪 session，用于后续推送事件路由
      _trackSession(sessionId)

      // 确保后台帧读取器已启动（持续接收服务端推送的 222 帧）
      try {
        const s = await getSocket()
        await ensurePreflight(s)
        startBackgroundReader(s)
        // 对齐 Vue 版：连接建立后启动 heartbeat 并重新 JOIN 断线前的频道
        startHeartbeat()
        void _rejoinPendingChannels()
      } catch (e) {
        // 桥接不可用不影响返回已有事件
        console.warn('[chat-bridge] Poll: bridge unavailable:', e.message)
      }

      // 从 session 事件队列中取出所有待投递事件
      let events = _drainSessionQueue(sessionId)

      // 长轮询：如果没有事件，等待一段时间后再次检查
      if (events.length === 0) {
        const checkIntervalMs = 1000
        const maxWaitMs = Math.min(timeoutMs, 15000)
        const startTime = Date.now()

        while (Date.now() - startTime < maxWaitMs) {
          await new Promise(r => setTimeout(r, checkIntervalMs))
          events = _drainSessionQueue(sessionId)
          if (events.length > 0) break
        }
      }

      json(res, 200, { events, hasMore: false })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      json(res, 502, { error: msg, events: [] })
    }
    return
  }

  if (req.method === 'GET' && url.pathname === '/history') {
    const conversationId = url.searchParams.get('conversation_id') || ''
    try {
      const limit = Math.min(Number(url.searchParams.get('limit')) || 50, 100)

      if (!conversationId) {
        json(res, 400, { error: 'missing_conversation_id' })
        return
      }

      // 从 COS 或数据库获取历史消息
      // 当前实现返回空数组，实际应该查询存储
      // 这里发送 HISTORY 命令获取历史
      const sessionId = req.headers.sessionid || '0'
      const channel = conversationId.startsWith('#') ? conversationId : `#${conversationId}`

      const resp = await withTlsLock(() =>
        withTimeout(
          sendIrcCommand({ cmd: 'HISTORY', channel, limit, ts: Math.floor(Date.now() / 1000) }, sessionId),
          TLS_TIMEOUT_MS,
          `TLS timeout`,
        ),
      )

      const messages = resp?.messages || resp?.history || []
      json(res, 200, messages)
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      if (isRecoverableBridgeQueryError(msg)) {
        json(res, 200, [])
        return
      }
      json(res, 502, { error: msg })
    }
    return
  }

  // GET /conversations — 获取用户群聊列表
  // 对齐 Vue 版 listMyChatRooms → sendAppProtoCommand('LIST_MY_CHAT_ROOMS')
  if (req.method === 'GET' && url.pathname === '/conversations') {
    const sessionId = req.headers.sessionid || '0'
    _trackSession(sessionId)

    // 1. 通过 LIST_MY_CHAT_ROOMS（230 协议）获取用户已加入的群聊
    let serverRooms = []
    try {
      const resp = await withTimeout(
        sendAppProtoCommandStandalone('LIST_MY_CHAT_ROOMS', {}, sessionId),
        TLS_TIMEOUT_MS,
        `TLS timeout listing rooms`,
      )
      // 归一化响应：对齐 Vue 版 normalizeMyChatRoomsFromResponse
      const list = resp?.rooms || resp?.chat_rooms || resp?.chatRooms
        || resp?.conversations || resp?.data || resp?.rows
        || (Array.isArray(resp) ? resp : [])
      if (Array.isArray(list)) {
        serverRooms = list.map((item) => {
          if (typeof item === 'string' || typeof item === 'number') return { id: String(item) }
          const id = String(item?.id || item?.room_id || item?.roomId || item?.conversation_id || '').trim()
          const title = String(item?.title || item?.name || item?.room_name || item?.roomName || '').trim()
          const kind = String(item?.kind || item?.type || 'group').trim()
          return id ? { id, title, kind } : null
        }).filter(Boolean)
      }
    } catch (e) {
      console.warn('[chat-bridge] LIST_MY_CHAT_ROOMS error:', e.message)
    }

    // 2. 合并本地已 JOIN 的频道和已发现的频道（确保不遗漏）
    const all = new Map()
    for (const room of serverRooms) {
      const id = room.id.startsWith('#') ? room.id : `#${room.id}`
      all.set(id, { id, name: room.title || id.replace(/^#/, ''), type: room.kind || 'group', topic: '', owner_user_id: '', created_at: null })
    }
    // 补充本地已 JOIN 的频道
    const prefix = `${sessionId}:`
    for (const k of joinedBySession) {
      if (k.startsWith(prefix)) {
        const chId = k.slice(prefix.length)
        if (!all.has(chId) && !chId.includes('workspace') && !chId.includes('personal')) {
          all.set(chId, { id: chId, name: chId.replace(/^#/, ''), type: 'group', topic: '', owner_user_id: '', created_at: null })
        }
      }
    }
    // 补充已发现的频道
    const disc = discoveredChannels.get(sessionId) || new Set()
    for (const ch of disc) {
      if (!all.has(ch) && !ch.includes('workspace') && !ch.includes('personal')) {
        all.set(ch, { id: ch, name: ch.replace(/^#/, ''), type: 'group', topic: '', owner_user_id: '', created_at: null })
      }
    }

    const conversations = [...all.values()]
      .filter(c => !c.id.includes('workspace') && !c.id.includes('personal'))
    console.log(`[chat-bridge] GET /conversations: ${conversations.length} channels (server=${serverRooms.length})`)
    json(res, 200, { conversations })
    return
  }

  // POST /conversations — 创建群聊
  if (req.method === 'POST' && url.pathname === '/conversations') {
    try {
      const body = await readJsonBody(req)
      const name = typeof body.name === 'string' ? body.name.trim() : ''
      const conversationType = typeof body.conversation_type === 'string' ? body.conversation_type.trim() : 'group'
      const sessionId = req.headers.sessionid || '0'

      if (!name) {
        json(res, 400, { error: 'missing_name' })
        return
      }

      console.log(`[chat-bridge] CREATE_CONVERSATION: name="${name}", type="${conversationType}", sessionId="${sessionId}"`)

      // CREATE_CONVERSATION 用 230 协议（CommandReq）独立 TCP 连接，对齐 Vue 版 sendAppProtoCommand。
      // 注意：不能用 221（IrcMessageReq）—— 它是 IRC 消息通道，期望 channel/message 字段，
      // 服务端对 CREATE_CONVERSATION 会返回 BAD_REQUEST: missing_channel_or_message，
      // 且该错误响应被当作正常 JSON 返回（不抛异常），会导致 fallback 逻辑失效，最终 502。
      const resp = await withTimeout(
        sendAppProtoCommandStandalone(
          'CREATE_CONVERSATION',
          { name, conversation_type: conversationType },
          sessionId,
        ),
        TLS_TIMEOUT_MS,
        `TLS timeout creating conversation`,
      )

      // 标准化响应格式：服务端返回的 conversation 可能是字符串（纯 room id）或对象，
      // 统一归一化为带 id 的对象，对齐 chatApi.js 的解析期望（conversation.id）
      const rawConv = resp?.conversation || resp?.room || resp?.channel || resp || {}
      const conversation =
        typeof rawConv === 'string'
          ? { id: rawConv }
          : {
              id: String(rawConv.id || rawConv.room_id || rawConv.roomId || ''),
              title: String(rawConv.title || rawConv.name || rawConv.room_name || rawConv.roomName || ''),
              kind: 'group',
              owner_user_id: String(rawConv.owner_user_id || rawConv.ownerUserId || ''),
              created_by: String(rawConv.created_by || rawConv.createdBy || ''),
              status: String(rawConv.status || 'active'),
              created_at: String(rawConv.created_at || rawConv.createdAt || ''),
              updated_at: String(rawConv.updated_at || rawConv.updatedAt || ''),
            }
      if (!conversation.id) {
        // 检查是否是 session_id 无效导致的错误
        if (resp?.ok === false && (resp?.code === 401 || /session_id/i.test(resp?.error || ''))) {
          throw new Error('登录已过期，请重新登录后再创建群聊')
        }
        throw new Error('创建群聊失败：' + (resp?.error || resp?.message || '服务端返回了无效响应'))
      }
      console.log(`[chat-bridge] CREATE_CONVERSATION extracted:`, JSON.stringify(conversation).slice(0, 300))
      json(res, 200, { ok: true, conversation, ts: resp?.ts })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      console.error('[chat-bridge] CREATE_CONVERSATION error:', msg)
      json(res, 502, { error: msg })
    }
    return
  }

  // POST /room/info — 获取房间信息
  if (req.method === 'POST' && url.pathname === '/room/info') {
    let roomId = ''
    try {
      const body = await readJsonBody(req)
      roomId = typeof body.room_id === 'string' ? body.room_id.trim() : ''
      const sessionId = req.headers.sessionid || '0'

      if (!roomId) {
        json(res, 400, { error: 'missing_room_id' })
        return
      }

      // GET_ROOM_INFO 是 230 命令，用独立 TCP 连接避免与 poll 串扰
      const resp = await withTimeout(
        sendAppProtoCommandStandalone('GET_ROOM_INFO', { room_id: roomId }, sessionId),
        TLS_TIMEOUT_MS,
        `TLS timeout`,
      )

      const roomInfo = resp?.room_info || resp?.info || resp || {}
      json(res, 200, roomInfo)
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      if (isRecoverableBridgeQueryError(msg)) {
        json(res, 200, {
          name: roomId.replace(/^#/, ''),
          room_name: roomId.replace(/^#/, ''),
          owner_user_id: '',
          members: [],
        })
        return
      }
      json(res, 502, { error: msg })
    }
    return
  }

  // POST /room/members — 获取成员列表
  if (req.method === 'POST' && url.pathname === '/room/members') {
    try {
      const body = await readJsonBody(req)
      const roomId = typeof body.room_id === 'string' ? body.room_id.trim() : ''
      const sessionId = req.headers.sessionid || '0'

      if (!roomId) {
        json(res, 400, { error: 'missing_room_id' })
        return
      }

      // LIST_ROOM_MEMBERS 是 230 命令，用独立 TCP 连接
      const resp = await withTimeout(
        sendAppProtoCommandStandalone('LIST_ROOM_MEMBERS', { room_id: roomId }, sessionId),
        TLS_TIMEOUT_MS,
        `TLS timeout`,
      )

      const members = resp?.members || resp?.room_members || []
      json(res, 200, { members })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      json(res, 502, { error: msg })
    }
    return
  }

  // POST /room/invite — 邀请成员
  if (req.method === 'POST' && url.pathname === '/room/invite') {
    try {
      const body = await readJsonBody(req)
      const roomId = typeof body.room_id === 'string' ? body.room_id.trim() : ''
      const memberKind = typeof body.member_kind === 'string' ? body.member_kind.trim() : 'user'
      const role = typeof body.role === 'string' ? body.role.trim() : 'member'
      const sessionId = req.headers.sessionid || '0'

      if (!roomId) {
        json(res, 400, { error: 'missing_room_id' })
        return
      }

      // 根据 member_kind 选择对应的字段名（对齐 Vue 版 chatRoomMemberService）
      const cmdPayload = {
        room_id: roomId,
        member_kind: memberKind,
        role,
      }
      if (memberKind === 'phone') {
        const phone = (body.phone || body.user_id || body.target_id || '').toString().trim()
        if (!phone) { json(res, 400, { error: 'missing_phone' }); return }
        cmdPayload.phone = phone
      } else if (memberKind === 'node') {
        const nodeId = (body.node_id || body.user_id || body.target_id || '').toString().trim()
        if (!nodeId) { json(res, 400, { error: 'missing_node_id' }); return }
        cmdPayload.node_id = nodeId
      } else {
        const userId = (body.user_id || body.target_id || '').toString().trim()
        if (!userId) { json(res, 400, { error: 'missing_user_id' }); return }
        cmdPayload.user_id = userId
      }

      // INVITE_MEMBER 是 230 命令，用独立 TCP 连接
      const resp = await withTimeout(
        sendAppProtoCommandStandalone('INVITE_MEMBER', cmdPayload, sessionId),
        TLS_TIMEOUT_MS,
        `TLS timeout`,
      )

      const members = resp?.members || resp?.room_members || []
      json(res, 200, { ok: true, members })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      json(res, 502, { error: msg })
    }
    return
  }

  // POST /room/remove — 移除成员
  if (req.method === 'POST' && url.pathname === '/room/remove') {
    try {
      const body = await readJsonBody(req)
      const roomId = typeof body.room_id === 'string' ? body.room_id.trim() : ''
      const memberKind = typeof body.member_kind === 'string' ? body.member_kind.trim() : 'user'
      const sessionId = req.headers.sessionid || '0'

      if (!roomId) {
        json(res, 400, { error: 'missing_room_id' })
        return
      }

      // 根据 member_kind 选择对应的字段名
      const cmdPayload = {
        room_id: roomId,
        member_kind: memberKind,
      }
      if (memberKind === 'node') {
        const nodeId = (body.node_id || body.user_id || body.target_id || '').toString().trim()
        if (!nodeId) { json(res, 400, { error: 'missing_node_id' }); return }
        cmdPayload.node_id = nodeId
      } else {
        const userId = (body.user_id || body.target_id || '').toString().trim()
        if (!userId) { json(res, 400, { error: 'missing_user_id' }); return }
        cmdPayload.user_id = userId
      }

      // REMOVE_MEMBER 是 230 命令，用独立 TCP 连接
      const resp = await withTimeout(
        sendAppProtoCommandStandalone('REMOVE_MEMBER', cmdPayload, sessionId),
        TLS_TIMEOUT_MS,
        `TLS timeout`,
      )

      const members = resp?.members || resp?.room_members || []
      json(res, 200, { ok: true, members })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      json(res, 502, { error: msg })
    }
    return
  }

  // ── 个人工作空间 ──
  // GET /workspace/personal — 获取个人工作空间 ID
  if (req.method === 'GET' && url.pathname === '/workspace/personal') {
    try {
      const sessionId = req.headers.sessionid || '0'
      const resp = await withTimeout(
        sendAppProtoCommandStandalone('PERSONAL_WORKSPACE_GET', {}, sessionId),
        TLS_TIMEOUT_MS,
        `TLS timeout`,
      )
      const conversation = resp?.conversation || resp || {}
      const workspaceId = conversation.id ? String(conversation.id) : ''
      json(res, 200, { workspaceId: workspaceId || null })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      json(res, 502, { error: msg })
    }
    return
  }

  // ── 个人任务 CRUD ──
  // GET /tasks/personal — 获取个人任务列表
  if (req.method === 'GET' && url.pathname === '/tasks/personal') {
    try {
      const sessionId = req.headers.sessionid || '0'
      const resp = await withTimeout(
        sendAppProtoCommandStandalone('PERSONAL_TASK_LIST', {}, sessionId),
        TLS_TIMEOUT_MS,
        `TLS timeout`,
      )
      const tasks = Array.isArray(resp?.tasks) ? resp.tasks : []
      json(res, 200, { tasks })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      json(res, 502, { error: msg })
    }
    return
  }

  // POST /tasks/personal — 创建个人任务
  if (req.method === 'POST' && url.pathname === '/tasks/personal') {
    try {
      const body = await readJsonBody(req)
      const sessionId = req.headers.sessionid || '0'
      const taskId = typeof body.id === 'string' ? body.id.trim() : `pt-${Date.now()}`
      const ownerUserId = typeof body.owner_user_id === 'string' ? body.owner_user_id.trim() : ''
      const creatorUserId = typeof body.creator_user_id === 'string' ? body.creator_user_id.trim() : ownerUserId
      const title = typeof body.title === 'string' ? body.title.trim() : '新任务'
      const summary = typeof body.summary === 'string' ? body.summary.trim() : ''
      const sourceConversationId = typeof body.source_conversation_id === 'string' ? body.source_conversation_id.trim() : ''
      const dueAt = typeof body.due_at === 'string' ? body.due_at.trim() : ''
      const status = typeof body.status === 'string' ? body.status.trim() : 'pending'
      const deliveryTargetJson = typeof body.delivery_target_json === 'string' ? body.delivery_target_json.trim() : ''

      const resp = await withTimeout(
        sendAppProtoCommandStandalone(
          'PERSONAL_TASK_CREATE',
          {
            id: taskId,
            ownerUserId: ownerUserId,
            creatorUserId: creatorUserId,
            title,
            summary,
            sourceConversationId,
            createdFromMessageId: '',
            dueAt,
            status,
            deliveryTargetJson,
            reminderId: '',
            cronId: '',
          },
          sessionId,
        ),
        TLS_TIMEOUT_MS,
        `TLS timeout creating task`,
      )

      const resultTaskId = resp?.taskId || taskId
      json(res, 200, { ok: true, taskId: resultTaskId })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      json(res, 502, { error: msg })
    }
    return
  }

  // PUT /tasks/personal/:id/status — 更新任务状态
  if (req.method === 'PUT' && url.pathname.startsWith('/tasks/personal/') && url.pathname.endsWith('/status')) {
    try {
      const body = await readJsonBody(req)
      const sessionId = req.headers.sessionid || '0'
      const taskId = url.pathname.split('/tasks/personal/')[1]?.replace('/status', '') || ''
      const ownerUserId = typeof body.owner_user_id === 'string' ? body.owner_user_id.trim() : ''
      const status = typeof body.status === 'string' ? body.status.trim() : ''

      if (!taskId || !ownerUserId || !status) {
        json(res, 400, { error: 'missing_task_id_or_owner_user_id_or_status' })
        return
      }

      await withTimeout(
        sendAppProtoCommandStandalone(
          'PERSONAL_TASK_SET_STATUS',
          {
            personalTaskId: taskId,
            ownerUserId,
            status,
          },
          sessionId,
        ),
        TLS_TIMEOUT_MS,
        `TLS timeout setting task status`,
      )

      json(res, 200, { ok: true })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      json(res, 502, { error: msg })
    }
    return
  }

  // PUT /tasks/personal/:id/reschedule — 重新调度任务
  if (req.method === 'PUT' && url.pathname.startsWith('/tasks/personal/') && url.pathname.endsWith('/reschedule')) {
    try {
      const body = await readJsonBody(req)
      const sessionId = req.headers.sessionid || '0'
      const taskId = url.pathname.split('/tasks/personal/')[1]?.replace('/reschedule', '') || ''
      const ownerUserId = typeof body.owner_user_id === 'string' ? body.owner_user_id.trim() : ''
      const dueTime = typeof body.due_time === 'string' ? body.due_time.trim() : ''

      if (!taskId || !ownerUserId || !dueTime) {
        json(res, 400, { error: 'missing_task_id_or_owner_user_id_or_due_time' })
        return
      }

      await withTimeout(
        sendAppProtoCommandStandalone(
          'PERSONAL_TASK_RESCHEDULE',
          {
            personalTaskId: taskId,
            ownerUserId,
            dueTime,
          },
          sessionId,
        ),
        TLS_TIMEOUT_MS,
        `TLS timeout rescheduling task`,
      )

      json(res, 200, { ok: true })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      json(res, 502, { error: msg })
    }
    return
  }

  // ── 设备绑定 ──
  // GET /devices — 获取已绑定设备列表
  if (req.method === 'GET' && url.pathname === '/devices') {
    try {
      const sessionId = req.headers.sessionid || '0'
      const resp = await withTimeout(
        sendAppProtoCommandStandalone('LIST_DEVICES', {}, sessionId),
        TLS_TIMEOUT_MS,
        `TLS timeout listing devices`,
      )
      const devices = Array.isArray(resp?.devices) ? resp.devices : []
      json(res, 200, { devices })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      json(res, 502, { error: msg })
    }
    return
  }

  // POST /devices/bind-sessions — 创建设备绑定会话
  if (req.method === 'POST' && url.pathname === '/devices/bind-sessions') {
    try {
      const body = await readJsonBody(req)
      const sessionId = req.headers.sessionid || '0'
      const deviceName = typeof body.device_name === 'string' ? body.device_name.trim() : ''
      const deviceType = typeof body.device_type === 'string' ? body.device_type.trim() : 'TV'
      const deviceFingerprint = typeof body.device_fingerprint === 'string' ? body.device_fingerprint.trim() : ''
      const resp = await withTimeout(
        sendAppProtoCommandStandalone(
          'CREATE_BIND',
          {
            device_type: deviceType || 'TV',
            device_name: deviceName || 'Living Room TV',
            device_fingerprint: deviceFingerprint || undefined,
          },
          sessionId,
        ),
        TLS_TIMEOUT_MS,
        `TLS timeout creating bind session`,
      )
      json(res, 200, { ok: true, ...resp })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      json(res, 502, { error: msg })
    }
    return
  }

  // GET /devices/bind-sessions/:token — 查询绑定会话
  if (req.method === 'GET' && url.pathname.startsWith('/devices/bind-sessions/')) {
    try {
      const sessionId = req.headers.sessionid || '0'
      const bindToken = decodeURIComponent(url.pathname.split('/devices/bind-sessions/')[1] || '').trim()
      if (!bindToken) {
        json(res, 400, { error: 'missing_bind_token' })
        return
      }
      const resp = await withTimeout(
        sendAppProtoCommandStandalone('GET_BIND', { bind_token: bindToken }, sessionId),
        TLS_TIMEOUT_MS,
        `TLS timeout loading bind session`,
      )
      json(res, 200, { ok: true, ...resp })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      json(res, 502, { error: msg })
    }
    return
  }

  // POST /devices/bind-sessions/:token/confirm — 确认绑定到指定会话
  if (req.method === 'POST' && url.pathname.startsWith('/devices/bind-sessions/') && url.pathname.endsWith('/confirm')) {
    try {
      const body = await readJsonBody(req)
      const sessionId = req.headers.sessionid || '0'
      const bindToken = decodeURIComponent(url.pathname.split('/devices/bind-sessions/')[1]?.replace('/confirm', '') || '').trim()
      const conversationId = typeof body.conversation_id === 'string' ? body.conversation_id.trim() : ''
      const conversationName = typeof body.conversation_name === 'string' ? body.conversation_name.trim() : ''
      const deviceName = typeof body.device_name === 'string' ? body.device_name.trim() : ''
      const ownerPhone = typeof body.owner_phone === 'string' ? body.owner_phone.trim() : ''

      if (!bindToken || !conversationId) {
        json(res, 400, { error: 'missing_bind_token_or_conversation_id' })
        return
      }

      await withTimeout(
        sendAppProtoCommandStandalone('MARK_SCANNED', { bind_token: bindToken }, sessionId),
        TLS_TIMEOUT_MS,
        `TLS timeout marking bind session scanned`,
      )

      const resp = await withTimeout(
        sendAppProtoCommandStandalone(
          'CONFIRM_BIND',
          {
            bind_token: bindToken,
            conversation_id: conversationId,
            device_name: deviceName || undefined,
            bind_role: 'owner',
          },
          sessionId,
        ),
        TLS_TIMEOUT_MS,
        `TLS timeout confirming bind session`,
      )

      json(res, 200, {
        ok: true,
        conversation_id: conversationId,
        conversation_name: conversationName,
        owner_phone: ownerPhone,
        ...resp,
      })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      json(res, 502, { error: msg })
    }
    return
  }

  // DELETE /devices/:id — 解绑设备
  if (req.method === 'DELETE' && url.pathname.startsWith('/devices/')) {
    try {
      const sessionId = req.headers.sessionid || '0'
      const deviceId = decodeURIComponent(url.pathname.split('/devices/')[1] || '').trim()
      if (!deviceId) {
        json(res, 400, { error: 'missing_device_id' })
        return
      }

      await withTimeout(
        sendAppProtoCommandStandalone('UNBIND', { node_id: deviceId }, sessionId),
        TLS_TIMEOUT_MS,
        `TLS timeout unbinding device`,
      )
      json(res, 200, { ok: true })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      json(res, 502, { error: msg })
    }
    return
  }

  // ─ 群帖子 CRUD ──
  // POST /posts — 创建帖子（对齐 Vue 版 CONVERSATION_POST_CREATE）
  if (req.method === 'POST' && url.pathname === '/posts') {
    try {
      const body = await readJsonBody(req)
      const conversationId = typeof body.conversationId === 'string' ? body.conversationId.trim() : ''
      const title = typeof body.title === 'string' ? body.title.trim() : ''
      const summary = typeof body.summary === 'string' ? body.summary.trim() : ''
      const taskKind = typeof body.taskKind === 'string' ? body.taskKind.trim() : 'notice'
      const actionType = typeof body.actionType === 'string' ? body.actionType.trim() : 'read'
      const resourceType = typeof body.resourceType === 'string' ? body.resourceType.trim() : 'none'
      const resourceUrl = typeof body.resourceUrl === 'string' ? body.resourceUrl.trim() : ''
      const deadlineAt = body.deadlineAt || ''
      const createdById = typeof body.createdById === 'string' ? body.createdById.trim() : ''
      const createdByName = typeof body.createdByName === 'string' ? body.createdByName.trim() : ''
      const postClientid = typeof body.id === 'string' ? body.id.trim() : ''
      const sessionId = req.headers.sessionid || '0'

      if (!conversationId || !title) {
        json(res, 400, { error: 'missing_conversation_id_or_title' })
        return
      }

      console.log(`[chat-bridge] CONVERSATION_POST_CREATE: conv="${conversationId}", title="${title}", taskKind="${taskKind}"`)

      // 对齐 Vue 版 conversationPostApi.createConversationPost
      const resp = await withTimeout(
        sendAppProtoCommandStandalone(
          'CONVERSATION_POST_CREATE',
          {
            id: postClientid,
            conversationId,
            title,
            summary,
            taskKind,
            actionType,
            resourceType,
            resourceUrl,
            deadlineAt: deadlineAt ? String(deadlineAt) : '',
            createdById,
            createdByName,
          },
          sessionId,
        ),
        TLS_TIMEOUT_MS,
        `TLS timeout creating post`,
      )

      const postId = String(resp?.postId ?? postClientid ?? '')
      json(res, 200, { ok: true, postId, post: { id: postId, ...(resp?.post || {}) } })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      console.error('[chat-bridge] CONVERSATION_POST_CREATE error:', msg)
      json(res, 502, { error: msg })
    }
    return
  }

  // GET /posts — 获取帖子列表（对齐 Vue 版 CONVERSATION_POST_LIST）
  if (req.method === 'GET' && url.pathname === '/posts') {
    const conversationId = url.searchParams.get('conversation_id') || url.searchParams.get('conversationId') || ''
    try {
      const sessionId = req.headers.sessionid || '0'

      if (!conversationId) {
        json(res, 400, { error: 'missing_conversation_id' })
        return
      }

      console.log(`[chat-bridge] CONVERSATION_POST_LIST: conv="${conversationId}"`)

      const resp = await withTimeout(
        sendAppProtoCommandStandalone('CONVERSATION_POST_LIST', { conversationId }, sessionId),
        TLS_TIMEOUT_MS,
        `TLS timeout listing posts`,
      )

      const posts = resp?.posts || []
      json(res, 200, { ok: true, posts })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      console.error('[chat-bridge] CONVERSATION_POST_LIST error:', msg)
      if (isRecoverableBridgeQueryError(msg)) {
        json(res, 200, { ok: true, posts: [] })
        return
      }
      json(res, 502, { error: msg })
    }
    return
  }

  // POST /posts/respond — 响应帖子（对齐 Vue 版 CONVERSATION_POST_RESPONSE）
  if (req.method === 'POST' && url.pathname === '/posts/respond') {
    try {
      const body = await readJsonBody(req)
      const conversationId = typeof body.conversationId === 'string' ? body.conversationId.trim() : ''
      const postId = typeof body.postId === 'string' ? body.postId.trim() : ''
      const actorUserId = typeof body.actorUserId === 'string' ? body.actorUserId.trim() : ''
      const actorName = typeof body.actorName === 'string' ? body.actorName.trim() : ''
      const actionType = typeof body.actionType === 'string' ? body.actionType.trim() : 'read'
      const responseType = typeof body.responseType === 'string' ? body.responseType.trim() : 'read'
      const confirmation = typeof body.confirmation === 'string' ? body.confirmation.trim() : ''
      const content = typeof body.content === 'string' ? body.content.trim() : ''
      const attachmentsJson = typeof body.attachmentsJson === 'string' ? body.attachmentsJson : '{}'
      const sessionId = req.headers.sessionid || '0'

      if (!postId || !conversationId) {
        json(res, 400, { error: 'missing_post_id_or_conversation_id' })
        return
      }

      console.log(`[chat-bridge] CONVERSATION_POST_RESPONSE: post="${postId}", type="${responseType}"`)

      const resp = await withTimeout(
        sendAppProtoCommandStandalone(
          'CONVERSATION_POST_RESPONSE',
          {
            conversationId,
            postId,
            actorUserId,
            actorName,
            actionType,
            responseType,
            confirmation,
            content,
            attachmentsJson,
          },
          sessionId,
        ),
        TLS_TIMEOUT_MS,
        `TLS timeout responding to post`,
      )

      const responseId = String(resp?.responseId ?? '')
      json(res, 200, { ok: true, responseId, response: { id: responseId, ...(resp?.response || {}) } })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      console.error('[chat-bridge] CONVERSATION_POST_RESPONSE error:', msg)
      json(res, 502, { error: msg })
    }
    return
  }

  // PUT /posts/:id/close — 关闭帖子（对齐 Vue 版 CONVERSATION_POST_CLOSE）
  if (req.method === 'PUT' && url.pathname.startsWith('/posts/') && url.pathname.endsWith('/close')) {
    try {
      const body = await readJsonBody(req)
      const postId = url.pathname.split('/')[2]
      const conversationId = typeof body.conversationId === 'string' ? body.conversationId.trim() : ''
      const sessionId = req.headers.sessionid || '0'

      if (!postId || !conversationId) {
        json(res, 400, { error: 'missing_post_id_or_conversation_id' })
        return
      }

      console.log(`[chat-bridge] CONVERSATION_POST_CLOSE: post="${postId}"`)

      const resp = await withTimeout(
        sendAppProtoCommandStandalone(
          'CONVERSATION_POST_CLOSE',
          {
            postId,
            conversationId,
          },
          sessionId,
        ),
        TLS_TIMEOUT_MS,
        `TLS timeout closing post`,
      )

      const post = resp?.post || {}
      json(res, 200, { ok: true, post })
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      console.error('[chat-bridge] CONVERSATION_POST_CLOSE error:', msg)
      json(res, 502, { error: msg })
    }
    return
  }

  // ─ OpenClaw Agent 桥接 ─
  // 发送用户消息到 TCP 服务器，然后轮询获取 AI 回复，以 SSE 流式返回
  if (req.method === 'POST' && url.pathname === '/api/openclaw-agent') {
    try {
      const body = await readJsonBody(req)
      const message = typeof body.message === 'string' ? body.message.trim() : ''
      const sessionIdRaw = body.sessionId ?? body.session_id ?? body.sessionKey ?? req.headers['sessionid'] ?? '0'
      const sessionId = String(sessionIdRaw).trim()
      const convIdRaw = body.conversationId ?? body.conversation_id ?? ''
      const conversationId = convIdRaw.startsWith('#') ? convIdRaw : `#${convIdRaw || 'default'}`

      if (!message) {
        json(res, 400, { error: 'missing_message' })
        return
      }

      console.log(`[openclaw-agent-bridge] -> message="${message.slice(0, 80)}" sessionId="${sessionId}" conversationId="${conversationId}"`)

      // 1. 发送 PRIVMSG 到 TCP 服务器
      const tsSeconds = Math.floor(Date.now() / 1000)
      try {
        await withTlsLock(() =>
          withTimeout(
            sendIrcPrivmsg(conversationId, message, sessionId, tsSeconds),
            TLS_TIMEOUT_MS,
            'TLS timeout sending PRIVMSG',
          ),
        )
      } catch (sendErr) {
        console.warn('[openclaw-agent-bridge] PRIVMSG send error:', sendErr.message)
      }

      // 2. 轮询获取 Agent 回复（最多等待 30 秒）
      const startTime = Date.now()
      const pollIntervalMs = 1500
      const maxWaitMs = 30000
      let agentText = ''
      let pollCount = 0

      await new Promise(r => setTimeout(r, 1000))

      while (Date.now() - startTime < maxWaitMs) {
        pollCount++
        try {
          const resp = await withTlsLock(() =>
            withTimeout(
              sendIrcCommand(
                { cmd: 'LIST', channel: conversationId, ts: Math.floor(Date.now() / 1000) },
                sessionId,
              ),
              TLS_TIMEOUT_MS,
              'TLS timeout polling LIST',
            ),
          )

          if (resp && typeof resp === 'object') {
            const event = String(resp.event || '').toUpperCase()
            if (event === 'MESSAGE' || event === 'PRIVMSG') {
              const author = String(resp.author || resp.sender || '')
              if (author === 'agent' || author === 'assistant' || author === 'bot') {
                agentText = typeof resp.message === 'string' ? resp.message : typeof resp.text === 'string' ? resp.text : ''
                if (agentText) break
              }
            }
            if (Array.isArray(resp.messages)) {
              for (const msg of resp.messages) {
                const author = String(msg.author || msg.sender || '')
                if (author === 'agent' || author === 'assistant' || author === 'bot') {
                  agentText = typeof msg.message === 'string' ? msg.message : typeof msg.text === 'string' ? msg.text : ''
                  if (agentText) break
                }
              }
              if (agentText) break
            }
          }
        } catch (pollErr) {
          console.warn(`[openclaw-agent-bridge] Poll #${pollCount} error:`, pollErr.message)
        }
        await new Promise(r => setTimeout(r, pollIntervalMs))
      }

      console.log(`[openclaw-agent-bridge] <- pollCount=${pollCount} elapsed=${Date.now() - startTime}ms textLen=${agentText.length}`)

      if (agentText) {
        const sseBody = [
          `data: ${JSON.stringify({ type: 'delta', text: agentText })}\n\n`,
          `data: ${JSON.stringify({ type: 'final', text: agentText, state: 'final' })}\n\n`,
        ].join('')

        res.writeHead(200, {
          'Content-Type': 'text/event-stream; charset=utf-8',
          'Cache-Control': 'no-cache',
          Connection: 'keep-alive',
          'Access-Control-Allow-Origin': '*',
          'Access-Control-Allow-Headers': 'Content-Type, sessionid',
        })
        res.write(sseBody)
        res.end()
      } else {
        json(res, 200, { ok: true, text: '', hint: 'no_agent_reply' })
      }
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      console.error('[openclaw-agent-bridge] ERROR:', msg)
      json(res, 502, { ok: false, error: msg })
    }
    return
  }

  // ── Auth API proxy: /api/auth/send-sms & /api/auth/login ──
  // Browser cannot connect to backend TLS directly (ERR_CERT_COMMON_NAME_INVALID),
  // so we proxy auth requests through this bridge using HBPC protocol.
  if (req.method === 'POST' && url.pathname === '/api/auth/send-sms') {
    try {
      const body = await readJsonBody(req)
      const phone = typeof body.phone === 'string' ? body.phone.trim() : ''
      if (!phone) {
        json(res, 400, { error: 'missing_phone' })
        return
      }
      const result = await sendHbpcAuthCommand(MsgType.PhoneOtpSend, { phone })
      res.writeHead(200, {
        'Content-Type': 'text/plain; charset=utf-8',
        'Access-Control-Allow-Origin': '*',
        Connection: 'close',
      })
      res.end(typeof result === 'string' ? result : JSON.stringify(result))
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      console.error('[chat-bridge] send-sms proxy error:', msg)
      json(res, 502, { error: msg })
    }
    return
  }

  if (req.method === 'POST' && url.pathname === '/api/auth/login') {
    try {
      const body = await readJsonBody(req)
      const phone = typeof body.phone === 'string' ? body.phone.trim() : ''
      const code = typeof body.code === 'string' ? body.code.trim() : ''
      if (!phone || !code) {
        json(res, 400, { error: 'missing_phone_or_code' })
        return
      }
      // PhoneOtpVerify(156) 为主流程，失败或返回 OTP_REQUIRES_TLS 时回退 LoginSms(153)
      let result
      try {
        result = await sendHbpcAuthCommand(MsgType.PhoneOtpVerify, { phone, code })
        const bodyStr = typeof result === 'string' ? result : JSON.stringify(result)
        if (bodyStr.includes('OTP_REQUIRES_TLS') || (result?.error && !result?.sessionId)) {
          throw new Error(bodyStr.slice(0, 100))
        }
      } catch (e) {
        console.warn('[chat-bridge] PhoneOtpVerify failed, trying LoginSms fallback:', e.message)
        result = await sendHbpcAuthCommand(MsgType.LoginSms, { phone, code })
      }
      res.writeHead(200, {
        'Content-Type': 'text/plain; charset=utf-8',
        'Access-Control-Allow-Origin': '*',
        Connection: 'close',
      })
      res.end(typeof result === 'string' ? result : JSON.stringify(result))
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e)
      console.error('[chat-bridge] login proxy error:', msg)
      json(res, 502, { error: msg })
    }
    return
  }

  json(res, 404, { error: 'not_found' })
}

/**
 * Send an auth command via HBPC protocol (PhoneOtpSend=155, PhoneOtpVerify=156).
 * Uses the auth server TLS socket with HBPC framing.
 */
async function sendHbpcAuthCommand(msgType, payload) {
  const payloadBuf = Buffer.from(JSON.stringify(payload), 'utf8')
  if (payloadBuf.length > MAX_PAYLOAD_SIZE) {
    throw new Error('auth payload too large')
  }
  const header = buildHeader(msgType, payloadBuf.length)
  const s = await getAuthSocket()
  // Auth server doesn't need preflight

  await new Promise((resolve, reject) => {
    s.write(Buffer.concat([header, payloadBuf]), (err) => (err ? reject(err) : resolve()))
  })

  const { header: h, body } = await readFrame(s)
  const text = body.toString('utf8')
  // Auth responses can be: PhoneOtpResponse(157), OtpResponse(106), LoginSmsResponse(154), AuthResponse(152)
  // Unified server may also respond with IrcMessageResp(222)
  const authResponseTypes = [
    MsgType.PhoneOtpResponse,
    MsgType.OtpResponse,
    MsgType.LoginSmsResponse,
    MsgType.AuthResponse,
    MsgType.IrcMessageResp,
  ]
  if (!authResponseTypes.includes(h.type)) {
    throw new Error(`unexpected auth response type=${h?.type} body="${text.slice(0, 200)}"`)
  }

  // Extract session_id from response header (bytes 16-23, big-endian u64)
  const responseSessionId = h.sessionId ? h.sessionId.toString() : '0'
  console.log(`[chat-bridge] sendHbpcAuthCommand response: type=${h.type}, sessionId=${responseSessionId}, body=${text.slice(0, 300)}`)

  let parsed
  try {
    parsed = JSON.parse(text)
  } catch {
    parsed = { raw: text }
  }

  // 所有登录类响应都附带 sessionId（对齐 Vue 版 AuthBridge.smsVerify 行为）
  if (msgType === MsgType.PhoneOtpVerify || msgType === MsgType.LoginSms) {
    return {
      raw: text,
      sessionId: responseSessionId,
      ...parsed,
    }
  }

  return parsed
}

/**
 * Proxy auth requests to backend via HTTPS REST API.
 */
async function proxyAuthRestRequest(path, body, sessionId) {
  const https = await import('node:https')
  const authHost = process.env.AUTH_HOST || '139.224.189.70'
  const authPort = Number(process.env.AUTH_PORT || 9443)

  return new Promise((resolve, reject) => {
    const payload = JSON.stringify(body)
    const options = {
      hostname: authHost,
      port: authPort,
      path,
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Content-Length': Buffer.byteLength(payload),
        'sessionId': sessionId,
      },
      rejectUnauthorized: false,
    }

    const req = https.request(options, (resp) => {
      const chunks = []
      resp.on('data', (chunk) => chunks.push(chunk))
      resp.on('end', () => {
        const raw = Buffer.concat(chunks).toString('utf8')
        try {
          resolve(JSON.parse(raw))
        } catch {
          resolve({ raw })
        }
      })
      resp.on('error', reject)
    })
    req.on('error', reject)
    req.write(payload)
    req.end()
  })
}

process.on('uncaughtException', (err) => {
  console.error('[chat-bridge] uncaughtException:', err.stack || err.message)
  process.exitCode = 1
  server.close(() => process.exit(1))
})

process.on('unhandledRejection', (reason) => {
  console.error('[chat-bridge] unhandledRejection:', reason instanceof Error ? (reason.stack || reason.message) : reason)
  process.exitCode = 1
  server.close(() => process.exit(1))
})

const server = http.createServer((req, res) => {
  console.log(`[chat-bridge] ${req.method} ${req.url} from ${req.socket.remoteAddress}`)
  void handleRequest(req, res).catch((err) => {
    try {
      if (!res.headersSent && !res.writableEnded) {
        json(res, 500, { ok: false, error: String(err) })
      }
    } catch {
      try {
        res.destroy()
      } catch {
        // ignore
      }
    }
  })
})

server.on('error', (err) => {
  console.error('[chat-bridge] server error:', err.stack || err.message)
  process.exitCode = 1
  try { server.close() } catch { /* ignore */ }
  process.exit(1)
})

server.listen(BRIDGE, '127.0.0.1', () => {
  // eslint-disable-next-line no-console
  console.log(
    `[chat-bridge] http://127.0.0.1:${BRIDGE}  →  ${USE_TLS ? 'tls' : 'tcp'}://${HOST}:${PORT}  (rejectUnauthorized=${REJECT_UNAUTHORIZED})`,
  )
  if (USE_TLS && PREFLIGHT_ENABLE) {
    // eslint-disable-next-line no-console
    console.log(
      `[chat-bridge] preflight enabled (reqType=${PREFLIGHT_REQ_TYPE}, respType=${PREFLIGHT_RESP_TYPE})`,
    )
  }
})
