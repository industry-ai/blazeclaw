/**
 * OpenClaw Agent push bridge — 定时提醒的入站入口。
 *
 * 职责：接收 OpenClaw cron 触发的 HTTP POST，校验 body，通过 AppProto 221 协议
 * 转发 AGENT_PUSH 到 chat-server（session_id=0 匿名连接）。
 *
 * 完整链路：
 *   OpenClaw cron 触发 → agentTurn exec PowerShell
 *   → POST /api/openclaw-agent-push（本文件）
 *   → TCP 连接 chat-server → AppProto 221 AGENT_PUSH → 房间广播
 *   → 浏览器 / Android 客户端收到提醒
 */
import crypto from 'node:crypto'
import type { IncomingMessage, ServerResponse } from 'node:http'
import {
  toStringValue,
  prefixCreatorMention,
  isRecord,
  resolveChatHost,
  resolveChatPort,
  resolvePushTimeoutMs,
  isObsoleteAgentPushChannel,
  sendChatServerRequest,
  sendAgentPushToChatServer,
  resolveOpenClawAgentPushToken,
  HBPC_IDEMPOTENCY_TTL_MS,
  HBPC_IDEMPOTENCY_MAX_ENTRIES,
  type AgentPushPayload,
} from './openclaw-shared'

const recentIdempotencyKeys = new Map<string, number>()

// ── 请求体识别与解析 ──

function directAgentPushCandidate(body: Record<string, unknown>): boolean {
  const channel = toStringValue(body.channel || body.conversationId)
  return Boolean(
    toStringValue(body.cmd).toUpperCase() === 'AGENT_PUSH' ||
    body.typing === true ||
    (channel.startsWith('#') && Boolean(toStringValue(body.message))),
  )
}

function parseJsonRecord(value: unknown): Record<string, unknown> | null {
  const text = toStringValue(value)
  if (!text) return null

  try {
    const parsed = JSON.parse(text)
    return isRecord(parsed) ? parsed : null
  } catch {
    const first = text.indexOf('{')
    const last = text.lastIndexOf('}')
    if (first < 0 || last <= first) return null
    try {
      const parsed = JSON.parse(text.slice(first, last + 1))
      return isRecord(parsed) ? parsed : null
    } catch {
      return null
    }
  }
}

const WEBHOOK_WRAPPER_KEYS = [
  'payload', 'job', 'summary', 'message', 'text', 'data', 'result', 'output', 'body',
]

function findAgentPushRequestBody(
  value: unknown,
  inheritedToken: unknown,
  depth = 0,
): Record<string, unknown> | null {
  if (depth > 4) return null

  const record = isRecord(value) ? value : parseJsonRecord(value)
  if (!record) return null

  const token = record.token || inheritedToken
  if (directAgentPushCandidate(record)) {
    return { ...record, ...(token ? { token } : {}) }
  }

  for (const key of WEBHOOK_WRAPPER_KEYS) {
    if (!(key in record)) continue
    const found = findAgentPushRequestBody(record[key], token, depth + 1)
    if (found) return found
  }

  return null
}

export function resolveAgentPushRequestBody(body: Record<string, unknown>): { body: Record<string, unknown>; source: string } {
  if (directAgentPushCandidate(body)) return { body, source: 'direct' }
  const parsed = findAgentPushRequestBody(body, body.token)
  if (parsed) return { body: parsed, source: 'openclaw-cron-webhook' }
  return { body, source: 'direct' }
}
export function ensureOpenClawAgentPushEnv(port = 3000): void {
  process.env.AGENTCHAT_BLAZECLAW_AGENT_PUSH_URL ||=
    toStringValue(process.env.BLAZECLAW_AGENT_PUSH_PUBLIC_URL || process.env.AGENTCHAT_BLAZECLAW_AGENT_PUSH_PUBLIC_URL) ||
    `http://127.0.0.1:${port}/api/blazeclaw-agent-push`
  process.env.AGENTCHAT_OPENCLAW_AGENT_PUSH_URL ||=
    toStringValue(process.env.OPENCLAW_AGENT_PUSH_PUBLIC_URL || process.env.AGENTCHAT_OPENCLAW_AGENT_PUSH_PUBLIC_URL) ||
    process.env.AGENTCHAT_BLAZECLAW_AGENT_PUSH_URL ||
    `http://127.0.0.1:${port}/api/openclaw-agent-push`
  const token = resolveOpenClawAgentPushToken()
  if (token) process.env.AGENTCHAT_BLAZECLAW_AGENT_PUSH_TOKEN ||= token
  if (token) process.env.AGENTCHAT_OPENCLAW_AGENT_PUSH_TOKEN ||= token
}

// ── HTTP 响应工具 ──

function writeJson(res: ServerResponse, statusCode: number, body: Record<string, unknown>): void {
  const raw = Buffer.from(JSON.stringify(body), 'utf8')
  res.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': raw.length,
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Headers': 'Content-Type',
    'Access-Control-Allow-Methods': 'POST,OPTIONS',
    Connection: 'close',
  })
  res.end(raw)
}

const MAX_BODY_BYTES = 128 * 1024

function readJsonBody(req: IncomingMessage): Promise<Record<string, unknown>> {
  return new Promise((resolve, reject) => {
    let raw = ''
    req.setEncoding('utf8')
    req.on('data', (chunk: string) => {
      raw += chunk
      if (Buffer.byteLength(raw, 'utf8') > MAX_BODY_BYTES) {
        reject(new Error('request body too large'))
        req.destroy()
      }
    })
    req.on('end', () => {
      try {
        resolve(raw ? JSON.parse(raw) as Record<string, unknown> : {})
      } catch {
        reject(new Error('request body is not valid JSON'))
      }
    })
    req.on('error', reject)
  })
}

// ── 请求校验 ──

export function validateAgentPushBody(body: Record<string, unknown>): AgentPushPayload {
  const cmd = toStringValue(body.cmd).toUpperCase()
  if (cmd && cmd !== 'AGENT_PUSH') {
    throw Object.assign(new Error('unsupported cmd'), { code: 'BAD_REQUEST' })
  }

  const token = resolveOpenClawAgentPushToken(body.token)
  const channel = toStringValue(body.channel || body.conversationId)
  const rawMessage = toStringValue(body.message)
  const typing = body.typing === true
  const idempotencyKey = toStringValue(body.idempotencyKey)
  const agentRequestId = toStringValue(body.agentRequestId) || crypto.randomUUID()
  const attachments = body.attachments
  const scope = toStringValue(body.scope)
  const eventType = toStringValue(body.eventType)
  const postId = toStringValue(body.postId)
  const personalTaskId = toStringValue(body.personalTaskId)
  const reminderId = toStringValue(body.reminderId)
  const creatorUserId = toStringValue(body.creatorUserId)
  const conversationId = toStringValue(body.conversationId)
  const deliverTo = isRecord(body.deliverTo) ? body.deliverTo : undefined
  const notifyMembers = body.notifyMembers === true
  const notification = isRecord(body.notification) ? body.notification : undefined
  const message = scope === 'group' ? rawMessage : prefixCreatorMention(rawMessage, channel, creatorUserId)

  if (!token) throw Object.assign(new Error('missing token'), { code: 'INVALID_TOKEN' })
  if (!channel || !channel.startsWith('#')) {
    throw Object.assign(new Error('missing or invalid channel'), { code: 'BAD_REQUEST' })
  }
  if (isObsoleteAgentPushChannel(channel)) {
    throw Object.assign(new Error(`refusing obsolete channel ${channel}`), { code: 'BAD_REQUEST' })
  }
  if (!typing && !message) throw Object.assign(new Error('missing message'), { code: 'BAD_REQUEST' })
  if (attachments !== undefined && !Array.isArray(attachments)) {
    throw Object.assign(new Error('attachments must be an array'), { code: 'BAD_REQUEST' })
  }

  return {
    cmd: 'AGENT_PUSH',
    token,
    channel,
    ...(typing ? { typing: true } : { message }),
    ...(idempotencyKey ? { idempotencyKey } : {}),
    ...(Array.isArray(attachments) ? { attachments } : {}),
    agentRequestId,
    ...(scope === 'personal' || scope === 'group' ? { scope } : {}),
    ...(eventType ? { eventType } : {}),
    ...(postId ? { postId } : {}),
    ...(personalTaskId ? { personalTaskId } : {}),
    ...(reminderId ? { reminderId } : {}),
    ...(creatorUserId ? { creatorUserId } : {}),
    ...(conversationId ? { conversationId } : {}),
    ...(deliverTo ? { deliverTo } : {}),
    ...(notifyMembers ? { notifyMembers: true } : {}),
    ...(notification ? { notification } : {}),
  }
}

// ── 错误/幂等 ──

function statusForErrorCode(code: unknown): number {
  switch (toStringValue(code).toUpperCase()) {
    case 'INVALID_TOKEN':
    case 'NOT_AUTHENTICATED':
      return 401
    case 'BAD_REQUEST':
      return 400
    case 'ROOM_NOT_FOUND':
      return 404
    case 'INTERNAL_ERROR':
      return 502
    default:
      return 502
  }
}

function pruneRecentIdempotencyKeys(nowMs = Date.now()): void {
  if (recentIdempotencyKeys.size <= HBPC_IDEMPOTENCY_MAX_ENTRIES) {
    for (const [key, seenAt] of recentIdempotencyKeys) {
      if (nowMs - seenAt > HBPC_IDEMPOTENCY_TTL_MS) recentIdempotencyKeys.delete(key)
    }
    return
  }

  const entries = [...recentIdempotencyKeys.entries()].sort((left, right) => left[1] - right[1])
  const removeCount = Math.max(entries.length - HBPC_IDEMPOTENCY_MAX_ENTRIES, 0)
  for (let i = 0; i < removeCount; i += 1) {
    const key = entries[i]?.[0]
    if (key) recentIdempotencyKeys.delete(key)
  }
}

export function rememberAgentPushIdempotencyKey(idempotencyKey: string, nowMs = Date.now()): boolean {
  const key = toStringValue(idempotencyKey)
  if (!key) return true
  pruneRecentIdempotencyKeys(nowMs)
  const seenAt = recentIdempotencyKeys.get(key)
  if (seenAt && nowMs - seenAt <= HBPC_IDEMPOTENCY_TTL_MS) return false
  recentIdempotencyKeys.set(key, nowMs)
  return true
}

function hasRecentAgentPushIdempotencyKey(idempotencyKey: string, nowMs = Date.now()): boolean {
  const key = toStringValue(idempotencyKey)
  if (!key) return false
  pruneRecentIdempotencyKeys(nowMs)
  const seenAt = recentIdempotencyKeys.get(key)
  return Boolean(seenAt && nowMs - seenAt <= HBPC_IDEMPOTENCY_TTL_MS)
}

export function clearAgentPushIdempotencyCache(): void {
  recentIdempotencyKeys.clear()
}

function buildDeduplicatedResponse(payload: AgentPushPayload): Record<string, unknown> {
  return {
    status: 'ok',
    event: 'AGENT_PUSH_ACK',
    channel: payload.channel,
    idempotencyKey: payload.idempotencyKey,
    deduplicated: true,
    message: 'duplicate AGENT_PUSH ignored by bridge',
  }
}

function describeAgentPushPayload(payload: AgentPushPayload): string {
  return [
    `channel=${payload.channel}`,
    `scope=${payload.scope ?? '-'}`,
    `eventType=${payload.eventType ?? '-'}`,
    `postId=${payload.postId ?? '-'}`,
    `personalTaskId=${payload.personalTaskId ?? '-'}`,
    `reminderId=${payload.reminderId ?? '-'}`,
    `idempotencyKey=${payload.idempotencyKey ?? '-'}`,
    `notifyMembers=${payload.notifyMembers === true}`,
    `typing=${payload.typing === true}`,
    `attachments=${payload.attachments?.length ?? 0}`,
  ].join(' ')
}

export async function handleOpenClawAgentPushBridgeRequest(
  req: IncomingMessage,
  res: ServerResponse,
): Promise<void> {
  if (req.method === 'OPTIONS') {
    writeJson(res, 204, { ok: true })
    return
  }

  if (req.method !== 'POST') {
    writeJson(res, 405, { ok: false, event: 'ERROR', code: 'BAD_REQUEST', message: 'Method not allowed' })
    return
  }

  let source = 'direct'
  let bodyKeys = '-'

  try {
    const body = await readJsonBody(req)
    bodyKeys = Object.keys(body).join(',') || '-'
    const resolved = resolveAgentPushRequestBody(body)
    source = resolved.source
    const payload = validateAgentPushBody(resolved.body)
    if (!payload.idempotencyKey) {
      console.warn(`[openclaw-agent-push-bridge] WARN missing idempotencyKey ${describeAgentPushPayload(payload)}`)
    } else if (hasRecentAgentPushIdempotencyKey(payload.idempotencyKey)) {
      const deduplicated = buildDeduplicatedResponse(payload)
      console.log(`[openclaw-agent-push-bridge] duplicate ignored source=${source} ${describeAgentPushPayload(payload)}`)
      writeJson(res, 200, deduplicated)
      return
    }

    console.log(`[openclaw-agent-push-bridge] -> AGENT_PUSH source=${source} ${describeAgentPushPayload(payload)} message="${payload.message ?? ''}" at=${new Date().toISOString()}`)
    const response = await sendAgentPushToChatServer(payload)
    if (payload.idempotencyKey) rememberAgentPushIdempotencyKey(payload.idempotencyKey)
    const responseEvent = toStringValue(response.event) || toStringValue(response.status)
    const responseCode = toStringValue(response.code)
    const responseMessage = toStringValue(response.message).replace(/\s+/g, ' ').slice(0, 160)
    console.log(`[openclaw-agent-push-bridge] <- ${responseEvent} channel=${toStringValue(response.channel) || payload.channel} code=${responseCode || '-'} message=${responseMessage || '-'} deduplicated=${String(response.deduplicated ?? false)}`)
    if (toStringValue(response.event).toUpperCase() === 'ERROR') {
      writeJson(res, statusForErrorCode(response.code), response)
      return
    }
    writeJson(res, 200, response)
  } catch (error) {
    const code = toStringValue((error as { code?: unknown })?.code) || 'BRIDGE_UPSTREAM_UNAVAILABLE'
    const message = error instanceof Error ? error.message : String(error)
    console.log(`[openclaw-agent-push-bridge] ERROR code=${code} source=${source} bodyKeys=${bodyKeys} message=${message}`)
    writeJson(res, statusForErrorCode(code), {
      event: 'ERROR',
      code,
      message,
    })
  }
}

// ── 重新导出 (供 bridge 和测试使用) ──
export { sendChatServerRequest, sendAgentPushToChatServer } from './openclaw-shared'
