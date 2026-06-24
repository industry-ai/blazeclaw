import type { IncomingMessage, ServerResponse } from 'node:http'

const DEFAULT_TTS_TIMEOUT_MS = 120_000

function toStringValue(value: unknown): string {
  return String(value ?? '').trim()
}

function resolveTimeoutMs(): number {
  const raw = Number(process.env.TTS_TIMEOUT_MS || process.env.MOSS_TTS_TIMEOUT_MS || 0)
  return Number.isFinite(raw) && raw > 0 ? raw : DEFAULT_TTS_TIMEOUT_MS
}

function resolveTtsEndpoint(): string {
  const configuredEndpoint = toStringValue(process.env.TTS_ENDPOINT || process.env.MOSS_TTS_ENDPOINT)
  const configuredBaseUrl = toStringValue(process.env.TTS_BASE_URL || process.env.MOSS_TTS_BASE_URL)
  if (configuredEndpoint) return configuredEndpoint
  if (!configuredBaseUrl) return ''
  return new URL('/chatroom/tts', configuredBaseUrl.endsWith('/') ? configuredBaseUrl : `${configuredBaseUrl}/`).toString()
}

function joinPublicAudioUrl(baseUrl: string, audioPath: string): string {
  const normalizedBase = baseUrl.replace(/\/+$/, '')
  const normalizedPath = audioPath.replace(/\\/g, '/')
  const fileName = normalizedPath.split('/').filter(Boolean).at(-1)
  return fileName ? `${normalizedBase}/${encodeURIComponent(fileName)}` : ''
}

export function resolveTtsAudioUrl(payload: Record<string, unknown>): string {
  for (const key of ['audioUrl', 'audio_url', 'url', 'publicUrl', 'public_url', 'outputUrl', 'output_url']) {
    const value = toStringValue(payload[key])
    if (/^https?:\/\//i.test(value)) return value
  }

  const audioPath = toStringValue(payload.audio_path || payload.audioPath || payload.output)
  if (/^https?:\/\//i.test(audioPath)) return audioPath

  const publicBaseUrl = toStringValue(process.env.TTS_AUDIO_PUBLIC_BASE_URL || process.env.MOSS_TTS_AUDIO_PUBLIC_BASE_URL)
  if (!audioPath || !publicBaseUrl) return ''
  return joinPublicAudioUrl(publicBaseUrl, audioPath)
}

async function readJsonBody(req: IncomingMessage): Promise<Record<string, unknown>> {
  let raw = ''
  req.setEncoding('utf8')
  for await (const chunk of req) {
    raw += chunk
    if (raw.length > 1024 * 1024) {
      throw new Error('TTS bridge request body is too large.')
    }
  }
  if (!raw.trim()) return {}
  const parsed = JSON.parse(raw) as unknown
  if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
    throw new Error('TTS bridge request body must be a JSON object.')
  }
  return parsed as Record<string, unknown>
}

function writeJsonResponse(res: ServerResponse, statusCode: number, body: Record<string, unknown>): void {
  const payload = Buffer.from(JSON.stringify(body), 'utf8')
  res.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': payload.length,
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Headers': 'Content-Type, Authorization',
    'Access-Control-Allow-Methods': 'POST, OPTIONS',
    Connection: 'close',
  })
  res.end(payload)
}

async function requestMossTts(body: Record<string, unknown>): Promise<Record<string, unknown>> {
  const endpoint = resolveTtsEndpoint()
  if (!endpoint) {
    throw new Error('TTS endpoint is not configured. Set TTS_BASE_URL or TTS_ENDPOINT.')
  }

  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), resolveTimeoutMs())
  try {
    const token = toStringValue(process.env.TTS_TOKEN || process.env.MOSS_TTS_TOKEN)
    const response = await fetch(endpoint, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json; charset=utf-8',
        ...(token ? { Authorization: `Bearer ${token}` } : {}),
      },
      body: JSON.stringify(body),
      signal: controller.signal,
    })
    const raw = await response.text()
    if (!response.ok) {
      throw new Error(`MOSS TTS request failed: HTTP ${response.status} ${raw.slice(0, 200)}`.trim())
    }
    const parsed = raw ? JSON.parse(raw) as unknown : {}
    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
      throw new Error('MOSS TTS returned a non-object response.')
    }
    return parsed as Record<string, unknown>
  } finally {
    clearTimeout(timer)
  }
}

export async function handleTtsSynthesizeRequest(req: IncomingMessage, res: ServerResponse): Promise<void> {
  if (req.method === 'OPTIONS') {
    writeJsonResponse(res, 204, { ok: true })
    return
  }

  if (req.method !== 'POST') {
    writeJsonResponse(res, 405, { ok: false, error: 'Method not allowed.' })
    return
  }

  try {
    const body = await readJsonBody(req)
    const text = toStringValue(body.text)
    if (!text) {
      writeJsonResponse(res, 400, { ok: false, error: 'text is required.' })
      return
    }

    const upstream = await requestMossTts({
      text,
      user_id: toStringValue(body.userId || body.user_id) || 'default_user',
      room_id: toStringValue(body.conversationId || body.roomId || body.room_id) || 'default_room',
    })
    const audioUrl = resolveTtsAudioUrl(upstream)
    if (!audioUrl) {
      writeJsonResponse(res, 502, {
        ok: false,
        error: 'MOSS TTS did not return a public audio URL.',
        hint: 'Return audio_url from moss_-tts, or set TTS_AUDIO_PUBLIC_BASE_URL so audio_path can be mapped to a public URL.',
        upstream,
      })
      return
    }

    writeJsonResponse(res, 200, {
      ok: true,
      source: 'moss-tts',
      text,
      audioUrl,
      taskId: toStringValue(upstream.task_id || upstream.taskId) || undefined,
      upstreamStatus: toStringValue(upstream.status) || undefined,
      upstreamSource: toStringValue(upstream.source) || undefined,
    })
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    writeJsonResponse(res, 502, { ok: false, error: message })
  }
}
