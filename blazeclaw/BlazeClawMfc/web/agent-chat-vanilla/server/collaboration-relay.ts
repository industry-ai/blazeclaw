import type { IncomingMessage, ServerResponse } from 'node:http'

type RelayAction = 'device.open_content' | 'device.speak' | 'ai.skill_result' | 'ai.task_status'

interface CollaborationInstruction {
  protocol?: unknown
  version?: unknown
  action?: unknown
  conversationId?: unknown
  dispatchId?: unknown
  dedupeKey?: unknown
  timestamp?: unknown
  payload?: unknown
  ttlMs?: unknown
}

export interface CurrentDisplayContent {
  conversationId: string
  displayId: string
  title: string
  summary?: string
  contentType: string
  url?: string
  speakText?: string
  related?: Record<string, unknown>
  updatedAt: number
}

export interface CollaborationRelayResult {
  ok: boolean
  status: 'accepted' | 'deduplicated' | 'ignored' | 'failed'
  code?: string
  message?: string
  currentDisplay?: CurrentDisplayContent
}

const currentDisplayByConversation = new Map<string, CurrentDisplayContent>()
const dedupeExpiresAtByKey = new Map<string, number>()

function trim(value: unknown): string {
  return String(value ?? '').trim()
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value)
}

function writeJson(res: ServerResponse, statusCode: number, body: Record<string, unknown>) {
  res.statusCode = statusCode
  res.setHeader('Content-Type', 'application/json; charset=utf-8')
  res.end(JSON.stringify(body))
}

async function readJsonBody(req: IncomingMessage): Promise<unknown> {
  const chunks: Buffer[] = []
  for await (const chunk of req) {
    chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk))
  }
  const text = Buffer.concat(chunks).toString('utf8').trim()
  if (!text) return {}
  return JSON.parse(text) as unknown
}

function isUrlAllowed(raw: string): boolean {
  if (!raw) return true
  try {
    const url = new URL(raw)
    return url.protocol === 'http:' || url.protocol === 'https:'
  } catch {
    return false
  }
}

function cleanupDedupe(now: number) {
  for (const [key, expiresAt] of dedupeExpiresAtByKey) {
    if (expiresAt <= now) dedupeExpiresAtByKey.delete(key)
  }
}

function validateInstruction(input: CollaborationInstruction, now: number): {
  ok: true
  action: RelayAction
  conversationId: string
  dedupeKey: string
  ttlMs: number
  payload: Record<string, unknown>
} | { ok: false; code: string; message: string } {
  if (input.protocol !== 'agentchat.collaboration') {
    return { ok: false, code: 'BAD_PROTOCOL', message: 'protocol must be agentchat.collaboration' }
  }
  if (input.version !== 1) {
    return { ok: false, code: 'BAD_VERSION', message: 'version must be 1' }
  }
  const action = trim(input.action) as RelayAction
  if (action !== 'device.open_content' && action !== 'device.speak' && action !== 'ai.skill_result' && action !== 'ai.task_status') {
    return { ok: false, code: 'UNSUPPORTED_ACTION', message: `unsupported action: ${action || '-'}` }
  }
  const conversationId = trim(input.conversationId)
  if (!conversationId) {
    return { ok: false, code: 'BAD_REQUEST', message: 'conversationId is required' }
  }
  const dedupeKey = trim(input.dedupeKey) || trim(input.dispatchId)
  if (!dedupeKey) {
    return { ok: false, code: 'BAD_REQUEST', message: 'dedupeKey or dispatchId is required' }
  }
  const ttlMs = Number(input.ttlMs)
  const normalizedTtlMs = Number.isFinite(ttlMs) && ttlMs > 0 ? ttlMs : 30_000
  const timestamp = Number(input.timestamp)
  if (Number.isFinite(timestamp) && timestamp > 0 && timestamp + normalizedTtlMs < now) {
    return { ok: false, code: 'EXPIRED', message: 'instruction expired' }
  }
  if (!isRecord(input.payload)) {
    return { ok: false, code: 'BAD_REQUEST', message: 'payload must be an object' }
  }
  return { ok: true, action, conversationId, dedupeKey, ttlMs: normalizedTtlMs, payload: input.payload }
}

function buildCurrentDisplay(input: {
  action: RelayAction
  conversationId: string
  displayId: string
  payload: Record<string, unknown>
  now: number
}): CurrentDisplayContent | null {
  if (input.action === 'ai.skill_result' || input.action === 'ai.task_status') {
    const event = trim(input.payload.event)
    const status = trim(input.payload.status)
    const summary = trim(input.payload.summary) || trim(input.payload.message) || `${event}: ${status}`
    const outputs = Array.isArray(input.payload.outputs) ? input.payload.outputs : []
    const firstWebview = outputs.find((o: unknown) => isRecord(o) && trim((o as Record<string, unknown>).type) === 'webview')
    const url = firstWebview && isRecord(firstWebview) ? trim((firstWebview as Record<string, unknown>).url) : undefined
    return {
      conversationId: input.conversationId,
      displayId: input.displayId,
      title: summary,
      summary: trim(input.payload.skillName) || trim(input.payload.skillId) || undefined,
      contentType: 'ai_result',
      url: url && isUrlAllowed(url) ? url : undefined,
      related: { taskNo: trim(input.payload.taskNo), skillId: trim(input.payload.skillId) },
      updatedAt: input.now,
    }
  }

  if (input.action === 'device.open_content') {
    const url = trim(input.payload.url || input.payload.resourceUrl)
    if (!isUrlAllowed(url)) {
      throw new Error(`unsupported url: ${url}`)
    }
    return {
      conversationId: input.conversationId,
      displayId: input.displayId,
      title: trim(input.payload.title) || '当前展示内容',
      summary: trim(input.payload.summary) || undefined,
      contentType: trim(input.payload.contentType) || 'webview',
      url: url || undefined,
      speakText: trim(input.payload.speakText) || undefined,
      related: isRecord(input.payload.related) ? input.payload.related : undefined,
      updatedAt: input.now,
    }
  }

  const text = trim(input.payload.displayText) || trim(input.payload.text)
  if (!text) return null
  return {
    conversationId: input.conversationId,
    displayId: input.displayId,
    title: text,
    contentType: 'text',
    speakText: trim(input.payload.text) || text,
    related: isRecord(input.payload.related) ? input.payload.related : undefined,
    updatedAt: input.now,
  }
}

export function acceptCollaborationInstruction(
  instruction: CollaborationInstruction,
  now = Date.now(),
): CollaborationRelayResult {
  cleanupDedupe(now)
  const validated = validateInstruction(instruction, now)
  if (!validated.ok) {
    return { ok: false, status: validated.code === 'EXPIRED' ? 'ignored' : 'failed', code: validated.code, message: validated.message }
  }

  const existingExpiry = dedupeExpiresAtByKey.get(validated.dedupeKey)
  if (existingExpiry && existingExpiry > now) {
    return {
      ok: true,
      status: 'deduplicated',
      code: 'DUPLICATED',
      currentDisplay: currentDisplayByConversation.get(validated.conversationId),
    }
  }

  dedupeExpiresAtByKey.set(validated.dedupeKey, now + validated.ttlMs)
  try {
    const currentDisplay = buildCurrentDisplay({
      action: validated.action,
      conversationId: validated.conversationId,
      displayId: trim(instruction.dispatchId) || validated.dedupeKey,
      payload: validated.payload,
      now,
    })
    if (currentDisplay) currentDisplayByConversation.set(validated.conversationId, currentDisplay)
    return { ok: true, status: 'accepted', currentDisplay: currentDisplay ?? undefined }
  } catch (error) {
    return {
      ok: false,
      status: 'failed',
      code: 'BAD_REQUEST',
      message: error instanceof Error ? error.message : String(error),
    }
  }
}

export function getCurrentDisplay(conversationId: string): CurrentDisplayContent | undefined {
  return currentDisplayByConversation.get(conversationId)
}

export function resetCollaborationRelayForTest() {
  currentDisplayByConversation.clear()
  dedupeExpiresAtByKey.clear()
}

export async function handleCollaborationRelayRequest(req: IncomingMessage, res: ServerResponse): Promise<void> {
  if (req.method === 'OPTIONS') {
    writeJson(res, 204, { ok: true })
    return
  }

  const url = new URL(req.url ?? '/', 'http://127.0.0.1')
  const path = url.pathname

  try {
    if (req.method === 'GET' && (path.endsWith('/current-display') || path === '/current-display' || path === '/')) {
      const conversationId = trim(url.searchParams.get('conversationId'))
      if (!conversationId) {
        writeJson(res, 400, { ok: false, error: 'conversationId is required' })
        return
      }
      writeJson(res, 200, {
        ok: true,
        currentDisplay: getCurrentDisplay(conversationId) ?? null,
      })
      return
    }

    if (req.method === 'POST' && (path.endsWith('/dispatch') || path === '/dispatch' || path === '/')) {
      const body = await readJsonBody(req)
      const instruction = isRecord(body) && isRecord(body.instruction) ? body.instruction : body
      const result = acceptCollaborationInstruction(instruction as CollaborationInstruction)
      writeJson(res, result.ok ? 200 : 400, result as unknown as Record<string, unknown>)
      return
    }

    writeJson(res, 404, { ok: false, error: 'Not found' })
  } catch (error) {
    writeJson(res, 500, {
      ok: false,
      error: error instanceof Error ? error.message : String(error),
    })
  }
}
