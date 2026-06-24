const DEFAULT_PREFIX = 'chat/homework'

export function sanitizePathSegment(value) {
  const normalized = String(value ?? '')
    .trim()
    .replace(/^#+/, '')
    .replace(/[^\w.-]+/g, '-')
    .replace(/-+/g, '-')
    .replace(/^-|-$/g, '')
  return normalized || 'unknown'
}

export function normalizeObjectKeyPrefix(value = process.env.COS_UPLOAD_PREFIX ?? DEFAULT_PREFIX) {
  return String(value ?? DEFAULT_PREFIX).trim().replace(/^\/+|\/+$/g, '') || DEFAULT_PREFIX
}

export function buildAgentResourcePathMarker(input) {
  const sourceAgent = sanitizePathSegment(input?.sourceAgent || input?.providerId)
  const taskNo = sanitizePathSegment(input?.taskNo)
  return `ai/${sourceAgent}/${taskNo}`
}

export function buildObjectKey(input) {
  const prefix = normalizeObjectKeyPrefix(input?.prefix)
  const conversationId = sanitizePathSegment(input?.conversationId)
  const postId = sanitizePathSegment(input?.postId)
  const fileName = sanitizePathSegment(input?.fileName)
  const ts = Number.isFinite(input?.nowMs) ? Number(input.nowMs) : Date.now()
  const hasAgentMarker = Boolean(String(input?.taskNo ?? '').trim())
  const marker = hasAgentMarker ? `${buildAgentResourcePathMarker(input)}/` : ''
  return `${prefix}/${marker}${conversationId}/${postId}/${ts}_${fileName}`
}
