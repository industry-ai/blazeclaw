/**
 * OpenClaw 共享模块。
 *
 * 从 openclaw-agent-bridge.ts 和 openclaw-agent-push-bridge.ts 提取的公共代码，
 * 供 openclaw-runner.ts、bridge 和 push-bridge 共享使用。
 */
import crypto from 'node:crypto'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import net from 'node:net'
import { WebSocket } from 'ws'

// ══════════════════════════════════════════════════════════════════
// 常量 & 默认配置
// ══════════════════════════════════════════════════════════════════

export const DEFAULT_GATEWAY_URL = 'ws://127.0.0.1:18789'
export const DEFAULT_PROTOCOL_VERSION = 4
export const DEFAULT_SCOPES = ['operator.read', 'operator.write', 'operator.admin', 'operator.pairing']
export const ED25519_SPKI_PREFIX = Buffer.from('302a300506032b6570032100', 'hex')
export const MAX_AGENTCHAT_REMINDER_DELAY_MS = 1000 * 60 * 60 * 24 * 30

// HBPC 协议常量
export const HBPC_HEADER_SIZE = 64
export const HBPC_PROTO_VERSION = 1
export const HBPC_IRC_MESSAGE_REQ = 221
export const HBPC_IRC_MESSAGE_RESP = 222
export const HBPC_MAX_PAYLOAD_SIZE = 64 * 1024
export const HBPC_PROTO_MAGIC = Buffer.from('HBPC', 'ascii')
export const HBPC_DEFAULT_AGENT_PUSH_TOKEN = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ98s7d2b9e3c5a1f0d4'
export const HBPC_IDEMPOTENCY_TTL_MS = 6 * 60 * 60 * 1000
export const HBPC_IDEMPOTENCY_MAX_ENTRIES = 2000

// ══════════════════════════════════════════════════════════════════
// 基础工具函数
// ══════════════════════════════════════════════════════════════════

export function toStringValue(value: unknown): string {
  return String(value ?? '').trim()
}

export function isProductionRuntime(): boolean {
  return toStringValue(process.env.NODE_ENV).toLowerCase() === 'production'
}

export function createRequestId(prefix = 'agent-chat-openclaw-bridge'): string {
  return `${prefix}-${crypto.randomUUID()}`
}

export function resolveTimeout(value: unknown, fallback: number): number {
  const parsed = Number(value)
  return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback
}

export function base64UrlEncode(buffer: Buffer): string {
  return buffer.toString('base64').replaceAll('+', '-').replaceAll('/', '_').replace(/=+$/g, '')
}

export function isRecord(value: unknown): value is Record<string, unknown> {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value)
}

// ══════════════════════════════════════════════════════════════════
// 文件系统工具
// ══════════════════════════════════════════════════════════════════

function resolveBlazeClawStateRoot(): string {
  const configured = toStringValue(
    process.env.BLAZECLAW_AGENTCHAT_STATE_DIR ||
    process.env.BLAZECLAW_STATE_DIR,
  )
  if (configured) return path.resolve(configured)

  const localAppData = toStringValue(process.env.LOCALAPPDATA)
  if (localAppData) {
    return path.join(localAppData, 'BlazeClaw', 'state', 'agent-chat', 'bridge')
  }

  return path.join(os.homedir(), '.blazeclaw', 'agent-chat', 'bridge')
}

export function statePath(...segments: string[]) {
  return path.join(resolveBlazeClawStateRoot(), ...segments)
}

export function readJson(filePath: string) {
  return JSON.parse(fs.readFileSync(filePath, 'utf8'))
}

// ══════════════════════════════════════════════════════════════════
// 频道 & 会话工具
// ══════════════════════════════════════════════════════════════════

export function isPersonalWorkspaceChannel(channel: string): boolean {
  return /(^#personal-workspace$|^#workspace[_-])/i.test(channel)
}

export function currentConversationChannel(body: Record<string, unknown>): string {
  const groupId = toStringValue(body.groupId)
  const conversationId = toStringValue(body.conversationId)
  if (conversationId.startsWith('#')) return conversationId
  if (groupId.startsWith('#')) return groupId
  return groupId || conversationId
}

export function requesterMentionFromBody(body: Record<string, unknown>): string {
  const visibleId =
    toStringValue(body.userPhone) ||
    toStringValue(body.creatorPhone) ||
    toStringValue(body.phone) ||
    toStringValue(body.userId || body.creatorUserId)
  if (!visibleId) return ''
  return `@${visibleId}`
}

export function shouldMentionRequester(body: Record<string, unknown>): boolean {
  const channel = currentConversationChannel(body)
  if (!channel.startsWith('#')) return false
  return !isPersonalWorkspaceChannel(channel)
}

export function prefixRequesterMention(text: string, body: Record<string, unknown>): string {
  const content = toStringValue(text)
  const mention = shouldMentionRequester(body) ? requesterMentionFromBody(body) : ''
  if (!mention || !content || content.startsWith(mention) || /^@\S+\s/u.test(content)) return content
  return `${mention} ${content}`
}

/** 从 push-bridge 复用——对个人消息加 @ 前缀 */
export function prefixCreatorMention(message: string, channel: string, creatorUserId: string): string {
  const content = toStringValue(message)
  const userId = toStringValue(creatorUserId)
  if (!content || !userId || !channel.startsWith('#')) return content
  if (/(^#personal-workspace$|^#workspace[_-])/i.test(channel)) return content
  const mention = `@${userId}`
  if (content.startsWith(mention) || /^@\S+\s/u.test(content)) return content
  return `${mention} ${content}`
}

// ══════════════════════════════════════════════════════════════════
// 配置解析
// ══════════════════════════════════════════════════════════════════

export function isExplicitOpenClawCardGenerationRequest(text: string): boolean {
  const query = toStringValue(text)
  return (
    /(?:H5\s*)?卡片|card|comic|漫画|预览|作业|homework|assignment/i.test(query) &&
    /生成|创建|制作|渲染|做一个|来一个|帮我|发一个|发给|send|create|generate|make|render/i.test(query)
  )
}

export function buildOpenClawRunnerMessage(body: Record<string, unknown>): string {
  const message = toStringValue(body.message)
  if (!message) return ''
  if (!isExplicitOpenClawCardGenerationRequest(message)) return message

  const payload: Record<string, unknown> = {
    input: { text: message },
    conversationId: toStringValue(body.conversationId) || undefined,
    groupId: toStringValue(body.groupId) || undefined,
    messageId: toStringValue(body.messageId) || undefined,
    requesterUserId: toStringValue(body.userId) || undefined,
    requesterPhone: toStringValue(body.userPhone) || undefined,
  }

  return [
    'AgentChat AI provider. Read AI_TASK_REQUEST JSON and answer in concise Simplified Chinese.',
    'Use tools only when needed. For generated files/images/audio/pages, return the final public http/https URL.',
    'For generated HTML/H5 card pages, use the h5-cards skill when available and return the public .html URL.',
    'The generated HTML URL must open directly in a browser/webview.',
    'Do not answer only "已处理完成，请查看当前结果。" when a generated URL is expected.',
    'Do not expose tool names, logs, code, PowerShell, base64, or debugging details.',
    '',
    'AI_TASK_REQUEST:',
    JSON.stringify(payload),
    '',
    'Card generation requirement:',
    'Return the generated card as a public http/https .html URL. The chat client will render that URL as a card.',
  ].join('\n')
}

export function fallbackForLowInformationCardResult(text: string, originalRequest: string): string {
  if (!isExplicitOpenClawCardGenerationRequest(originalRequest)) return text
  if (!isLowInformationOpenClawFinalText(text)) return text
  return '卡片生成没有返回可打开链接，请稍后重试。'
}

export function resolveProtocolVersion(): number {
  const env = process.env.BLAZECLAW_PROTOCOL_VERSION || process.env.OPENCLAW_PROTOCOL_VERSION
  if (env) {
    const parsed = Number(env)
    if (Number.isFinite(parsed) && parsed > 0) return parsed
  }
  return DEFAULT_PROTOCOL_VERSION
}

export function tryExtractProtocolMismatchVersion(error: unknown): number | undefined {
  if (!(error instanceof Error)) return undefined
  const details = (error as unknown as Record<string, unknown>).details as Record<string, unknown> | undefined
  if (details?.code === 'PROTOCOL_MISMATCH' && typeof details.expectedProtocol === 'number') {
    return details.expectedProtocol as number
  }
  return undefined
}

export function resolveGatewayUrl(): string {
  const raw =
    process.env.BLAZECLAW_GATEWAY_URL ||
    process.env.BLAZECLAW_GATEWAY_TARGET ||
    process.env.OPENCLAW_GATEWAY_URL ||
    process.env.VITE_OPENCLAW_GATEWAY_TARGET ||
    process.env.VITE_OPENCLAW_PROXY_TARGET ||
    DEFAULT_GATEWAY_URL
  const value = toStringValue(raw)
  if (!value || value.startsWith('/')) return DEFAULT_GATEWAY_URL
  return value
}

export function resolveChatHost(): string {
  return toStringValue(
    process.env.BLAZECLAW_AGENT_PUSH_CHAT_HOST ||
    process.env.AGENTCHAT_BLAZECLAW_AGENT_PUSH_CHAT_HOST ||
    process.env.OPENCLAW_AGENT_PUSH_CHAT_HOST ||
    process.env.AGENTCHAT_OPENCLAW_AGENT_PUSH_CHAT_HOST ||
    process.env.CHAT_TCP_HOST ||
    process.env.VITE_CHAT_TCP_HOST ||
    process.env.CHAT_TLS_HOST ||
    '101.132.254.212',
  )
}

export function resolveChatPort(): number {
  const value = Number(
    process.env.BLAZECLAW_AGENT_PUSH_CHAT_PORT ||
    process.env.AGENTCHAT_BLAZECLAW_AGENT_PUSH_CHAT_PORT ||
    process.env.OPENCLAW_AGENT_PUSH_CHAT_PORT ||
    process.env.AGENTCHAT_OPENCLAW_AGENT_PUSH_CHAT_PORT ||
    process.env.CHAT_TCP_PORT ||
    process.env.VITE_CHAT_TCP_PORT ||
    process.env.CHAT_TLS_PORT ||
    8765,
  )
  return Number.isInteger(value) && value > 0 && value <= 65535 ? value : 8765
}

export function resolvePushTimeoutMs(): number {
  const value = Number(
    process.env.BLAZECLAW_AGENT_PUSH_TIMEOUT_MS ||
    process.env.OPENCLAW_AGENT_PUSH_TIMEOUT_MS ||
    process.env.CHAT_TLS_TIMEOUT_MS ||
    30000,
  )
  return Number.isFinite(value) && value > 0 ? value : 30000
}

export function resolveAgentPushUrl(): string {
  const configured = toStringValue(
    process.env.BLAZECLAW_AGENT_PUSH_PUBLIC_URL ||
    process.env.AGENTCHAT_BLAZECLAW_AGENT_PUSH_PUBLIC_URL ||
    process.env.BLAZECLAW_AGENT_PUSH_BRIDGE_URL ||
    process.env.AGENTCHAT_BLAZECLAW_AGENT_PUSH_URL ||
    process.env.BLAZECLAW_AGENT_PUSH_URL ||
    process.env.OPENCLAW_AGENT_PUSH_PUBLIC_URL ||
    process.env.AGENTCHAT_OPENCLAW_AGENT_PUSH_PUBLIC_URL ||
    process.env.OPENCLAW_AGENT_PUSH_BRIDGE_URL ||
    process.env.AGENTCHAT_OPENCLAW_AGENT_PUSH_URL ||
    process.env.OPENCLAW_AGENT_PUSH_URL,
  )
  if (configured && !configured.includes(`/api/${'agent-push'}`)) return configured
  const port = toStringValue(process.env.PORT || process.env.VITE_PORT) || '3000'
  return `http://127.0.0.1:${port}/api/blazeclaw-agent-push`
}

export function resolveOpenClawAgentPushToken(bodyToken?: unknown): string {
  const configured = toStringValue(
    bodyToken ||
    process.env.BLAZECLAW_AGENT_PUSH_TOKEN ||
    process.env.AGENTCHAT_BLAZECLAW_AGENT_PUSH_TOKEN ||
    process.env.OPENCLAW_AGENT_PUSH_TOKEN ||
    process.env.AGENTCHAT_OPENCLAW_AGENT_PUSH_TOKEN,
  )
  if (configured) return configured
  return isProductionRuntime() ? '' : HBPC_DEFAULT_AGENT_PUSH_TOKEN
}

export function isObsoleteAgentPushChannel(channel: string): boolean {
  return channel === '#group-posts-demo' || channel === '#personal-workspace'
}

export function conversationTaskScope(channel: string): 'personal' | 'group' {
  return isPersonalWorkspaceChannel(channel) ? 'personal' : 'group'
}

// ══════════════════════════════════════════════════════════════════
// 消息文字清理
// ══════════════════════════════════════════════════════════════════

export function normalizeAgentMentionText(value: string): string {
  return value
    .replaceAll('@炎图AI助手', '')
    .replaceAll(`@炎图AI​助手`, '')
    .replaceAll('炎图AI助手', '')
    .replace(/\s+/g, ' ')
    .trim()
}

const OPENCLAW_EXECUTION_LEAK_RE = /\b(base64|delivery\s+mode|webhook|cron\s+job|powershell|invoke-restmethod|agentchat_push_ok|let\s+me|now\s+let\s+me|tool|execute|none)\b/i
const OPENCLAW_CHINESE_PROCESS_RE = /(?:让我|我来|正在|尝试|查找|检索|读取|调用|执行|使用|分析|检查|找到了|暂时无法正常检索)/u
const OPENCLAW_MOJIBAKE_RE = /(锛|銆|鍔|鑰|绋|嬫|傛|笉|闂|鍚|搴|勭|堕|硅|秴|嬪|鐨|浠|诲|殑|夸|娆|�)/

function isMojibakeText(text: string): boolean {
  const matches = text.match(new RegExp(OPENCLAW_MOJIBAKE_RE.source, 'g'))
  return (matches?.length ?? 0) >= 2
}

function isMostlyEnglishText(text: string): boolean {
  const withoutUrls = text.replace(/https?:\/\/\S+/gi, '')
  const chineseCount = (withoutUrls.match(/[一-鿿]/g) ?? []).length
  // Never reject text with meaningful Chinese content
  if (chineseCount >= 6) return false
  const latinCount = (withoutUrls.match(/[a-z]/gi) ?? []).length
  return latinCount > 30 && latinCount > chineseCount * 1.5
}

function extractJsonObjectText(text: string): string | null {
  const start = text.indexOf('{')
  if (start === -1) return null

  let depth = 0
  let inString = false
  let escaped = false
  for (let index = start; index < text.length; index += 1) {
    const char = text[index]
    if (escaped) {
      escaped = false
      continue
    }
    if (char === '\\') {
      escaped = true
      continue
    }
    if (char === '"') {
      inString = !inString
      continue
    }
    if (inString) continue
    if (char === '{') depth += 1
    if (char === '}') {
      depth -= 1
      if (depth === 0) return text.slice(start, index + 1)
    }
  }

  return null
}

function isStructuredCardPayloadText(text: string): boolean {
  const jsonText = extractJsonObjectText(text.trim())
  if (!jsonText) return false
  try {
    const parsed = JSON.parse(jsonText) as unknown
    if (!parsed || typeof parsed !== 'object') return false
    const record = parsed as Record<string, unknown>
    if (toStringValue(record.templateId ?? record.template_id) && toStringValue(record.title)) return true
    if (Array.isArray(record.attachments)) {
      return record.attachments.some((attachment) => {
        if (!attachment || typeof attachment !== 'object') return false
        return toStringValue((attachment as Record<string, unknown>).type) === 'ai_card'
      })
    }
  } catch {
    return false
  }
  return false
}

function hasAiProtocolJsonSignature(text: string): boolean {
  return /"(?:outputs|providerId|skillId)"\s*:/.test(text) && /"(?:summary|status)"\s*:/.test(text)
}

function tryExtractAiSkillResultText(raw: string): string | null {
  // Check for explicit AI_SKILL_RESULT or truncated variants
  const explicitIdx = raw.indexOf('AI_SKILL_RESULT')
  const hasSignature = hasAiProtocolJsonSignature(raw)
  if (explicitIdx === -1 && !hasSignature) return null

  // If no explicit marker, locate via signature fields
  const searchFrom = explicitIdx !== -1 ? explicitIdx : Math.min(
    raw.indexOf('"outputs"') !== -1 ? raw.indexOf('"outputs"') : Infinity,
    raw.indexOf('"providerId"') !== -1 ? raw.indexOf('"providerId"') : Infinity,
    raw.indexOf('"skillId"') !== -1 ? raw.indexOf('"skillId"') : Infinity,
  )
  if (searchFrom === Infinity) return null

  // Walk FORWARD from the marker to find the opening brace of the JSON object
  // (the JSON comes AFTER the marker text like "AI_SKILL_RESULT:\n```json\n")
  let start = searchFrom
  while (start < raw.length) {
    if (raw[start] === '{') break
    start += 1
  }
  if (start >= raw.length || raw[start] !== '{') return null

  // Walk forward to find the matching closing brace
  let depth = 0
  let inString = false
  let escaped = false
  for (let i = start; i < raw.length; i += 1) {
    const ch = raw[i]
    if (escaped) { escaped = false; continue }
    if (ch === '\\') { escaped = true; continue }
    if (ch === '"') { inString = !inString; continue }
    if (inString) continue
    if (ch === '{') depth += 1
    if (ch === '}') {
      depth -= 1
      if (depth === 0) {
        const jsonStr = raw.slice(start, i + 1)
        // Try strict parse first
        try {
          const parsed = JSON.parse(jsonStr)
          if (parsed && typeof parsed === 'object') {
            const summary = toStringValue(parsed.summary)
            let outputs: unknown[] = []
            if (Array.isArray(parsed.outputs)) {
              outputs = parsed.outputs
            } else if (parsed.outputs && typeof parsed.outputs === 'object') {
              // Handle outputs as a single object instead of array
              outputs = [parsed.outputs]
            }
            const textParts = outputs
              .filter((o: unknown) => o && typeof o === 'object')
              .flatMap((o: unknown) => {
                const rec = o as Record<string, unknown>
                // Standard format: { type: "text", content: "..." }
                if (rec.type === 'text') {
                  const c = toStringValue(rec.content)
                  return c ? [c] : []
                }
                // Preserve webview URLs so the client can extract them as attachments
                if (rec.type === 'webview' || rec.type === 'webview_content') {
                  const url = toStringValue(rec.url)
                  const title = toStringValue(rec.title)
                  if (url) {
                    const prefix = title ? `${title} ` : ''
                    return [`${prefix}${url}`]
                  }
                  return []
                }
                // Any output with a url field — preserve it
                const url = toStringValue(rec.url)
                if (url) return [url]
                // Direct format: { text: "..." }
                if (typeof rec.text === 'string') return [rec.text]
                return []
              })
              .filter(Boolean)
            const result = textParts.join('\n') || summary
            if (result) return result
          }
        } catch {
          // JSON malformed — fall through to regex extraction
        }

        // Regex fallback for malformed JSON
        const summaryMatch = jsonStr.match(/"summary"\s*:\s*"((?:[^"\\]|\\.)*)"/)
        const summary = summaryMatch ? summaryMatch[1].replace(/\\"/g, '"').replace(/\\n/g, '\n') : ''
        const contentMatch = jsonStr.match(/"content"\s*:\s*"((?:[^"\\]|\\.)*)"/)
        const content = contentMatch ? contentMatch[1].replace(/\\"/g, '"').replace(/\\n/g, '\n') : ''
        const textMatch = jsonStr.match(/"text"\s*:\s*"((?:[^"\\]|\\.)*)"/)
        const text = textMatch ? textMatch[1].replace(/\\"/g, '"').replace(/\\n/g, '\n') : ''
        const result = content || text || summary
        if (result) return result
      }
    }
  }
  return null
}

function stripOpenClawProcessNarration(text: string): string {
  if (!OPENCLAW_CHINESE_PROCESS_RE.test(text)) return text

  const answerStart = text.search(/(?:✅\s*)?(根据|结论[:：]|回答[:：]|结果[:：]|最终答案[:：]|已为您|已设置|已创建|详情[:：]|时间[:：]|消息[:：]|语音已生成|图片已生成|文件已生成)/u)
  if (answerStart >= 0) return text.slice(answerStart)

  return text
}

export function sanitizeOpenClawVisibleText(rawText: string, fallback = '已处理完成。'): string {
  const text = toStringValue(rawText)
  if (!text) return ''
  if (isStructuredCardPayloadText(text)) return text.trim()
  if (isMojibakeText(text)) return fallback

  const aiSkillText = tryExtractAiSkillResultText(text)
  console.log(`[sanitize] textLen=${text.length} hasAI=${text.includes('AI_SKILL_RESULT')} hasOutputs=${text.includes('"outputs"')} extracted=${!!aiSkillText} extractedLen=${aiSkillText?.length ?? 0}`)
  if (aiSkillText) {
    console.log(`[sanitize] EXTRACTED: "${aiSkillText.slice(0, 120)}"`)
    return aiSkillText
  }
  console.log(`[sanitize] FALLBACK textPreview="${text.slice(0, 200).replace(/\n/g, '\\n')}"`)

  let cleaned = stripOpenClawProcessNarration(text)
  if (OPENCLAW_EXECUTION_LEAK_RE.test(cleaned)) {
    const chineseStart = cleaned.search(/(?:✅\s*)?(已为您|已设置|已创建|详情[:：]|时间[:：]|消息[:：])/u)
    if (chineseStart >= 0) cleaned = cleaned.slice(chineseStart)
    else return fallback
  }

  cleaned = cleaned
    .replace(/\b(?:base64|delivery\s+mode|webhook|cron\s+job|powershell|invoke-restmethod|agentchat_push_ok)\b/gi, '')
    .replace(/^✅\s*/u, '')
    .replace(/[ \t]+\n/g, '\n')
    .trim()

  if (!cleaned || isMojibakeText(cleaned) || isMostlyEnglishText(cleaned)) return fallback
  return cleaned
}

export function sanitizeOpenClawDeltaText(rawText: string): string {
  let text = toStringValue(rawText)
  if (!text || isMojibakeText(text) || OPENCLAW_EXECUTION_LEAK_RE.test(text)) return ''
  if (OPENCLAW_CHINESE_PROCESS_RE.test(text)) return ''

  // Suppress markdown code block markers
  if (/^```(?:json)?\s*$/.test(text.trim())) return ''

  // Suppress protocol JSON fragments during streaming — match recognisable AI_SKILL_RESULT fields
  if (/AI_SKILL_RESULT|"(?:event|error|bizPayload|summary|outputs|skillId|taskNo|providerId|conversationId|status|messageId|requesterUserId|assistantId|skillName|workspaceId)"\s*:/.test(text)) return ''

  // Suppress pure-JSON deltas: contain protocol keywords + JSON syntax, but no Chinese content
  if (/\b(?:outputs|bizPayload|summary|skillId|skillName|event|taskNo|providerId|conversationId|status|messageId|requesterUserId|assistantId|workspaceId|location)\b/.test(text) &&
      /["{}:]/.test(text) &&
      !/[一-鿿]/.test(text)) return ''

  // Suppress isolated JSON structural fragments (opening braces, field-value separators)
  if (/^[\{\}\[\],:"\s]+$/.test(text.trim())) return ''

  // Strip leading JSON/markdown syntax from delta that also contains real content
  // e.g. `s":{"text":"武汉...` → `武汉...`, `":"天气` → `天气`
  text = text.replace(/^```(?:json)?\s*/, '')
  // Strip JSON key-value prefix patterns greedily until hitting readable content
  text = text.replace(/^(?:["'{\[\]},:\s]|\w+["':])+/, '')
  // Final fallback strip of any remaining non-content chars
  text = text.replace(/^[^一-鿿a-zA-Z0-9]+/, '')

  if (!text || isMostlyEnglishText(text)) return ''
  return text
}

// ══════════════════════════════════════════════════════════════════
// 提醒意图解析
// ══════════════════════════════════════════════════════════════════

export interface AgentChatReminderIntent {
  delayMs: number
  delayLabel: string
  reminderText: string
}

export interface AgentChatGroupTaskIntent extends AgentChatReminderIntent {
  triggerAtMs: number
  title: string
}

function parseChineseInteger(value: string): number | undefined {
  const raw = value.trim()
  if (!raw) return undefined
  if (/^\d+$/u.test(raw)) return Number(raw)
  if (raw === '半') return 0.5

  const digits: Record<string, number> = {
    零: 0, 一: 1, 二: 2, 两: 2, 三: 3, 四: 4,
    五: 5, 六: 6, 七: 7, 八: 8, 九: 9,
  }
  if (raw === '十') return 10
  const tenIndex = raw.indexOf('十')
  if (tenIndex >= 0) {
    const left = raw.slice(0, tenIndex)
    const right = raw.slice(tenIndex + 1)
    const tens = left ? digits[left] : 1
    const ones = right ? digits[right] : 0
    if (tens === undefined || ones === undefined) return undefined
    return tens * 10 + ones
  }
  return digits[raw]
}

function delayMsForReminderAmount(amount: number, unit: string): number {
  if (unit.startsWith('秒')) return amount * 1000
  if (unit === '分' || unit.startsWith('分钟')) return amount * 60 * 1000
  if (unit === '时' || unit.startsWith('小时')) return amount * 60 * 60 * 1000
  if (unit.startsWith('天')) return amount * 24 * 60 * 60 * 1000
  return 0
}

export function isExplicitPersonalReminderText(rawText: string): boolean {
  const text = normalizeAgentMentionText(rawText)
  if (!text) return false
  return /(提醒|通知|告诉|叫)\s*(一下)?\s*我|叫我|remind\s+me/iu.test(text)
}

function isPublishNoticeIntentText(text: string): boolean {
  return /^(?:请|帮我|麻烦)?\s*(?:发布|发|发送|创建)\s*(?:一条|个)?\s*(?:通知|公告|群公告|系统通知|资讯|news)(?=\s|[:：，,。]|$)/iu.test(text)
}

export function isExplicitGroupReminderText(rawText: string): boolean {
  const text = normalizeAgentMentionText(rawText)
  if (!text) return false
  if (isPublishNoticeIntentText(text)) return false
  if (isExplicitPersonalReminderText(text)) return false
  const hasReminderKeyword = /(提醒|通知|告诉|叫|定时|闹钟|timer|remind)/iu.test(text)
  const hasTimeExpression =
    /(\d+|半|[一二两三四五六七八九十]{1,3})\s*(秒钟|秒|分钟|分|小时|时|天)\s*(?:之后|以后|后)/u.test(text) ||
    /明天\s*(早上|上午|中午|下午|晚上|夜里)?\s*(\d{1,2}|[一二两三四五六七八九十]{1,3})\s*(?:点|时)/u.test(text) ||
    /(?:今天|今日|今晚|晚上|下午|中午|上午|早上)?\s*(\d{1,2}|[一二两三四五六七八九十]{1,3})(?::|：|点|时)(\d{1,2}|半)?/u.test(text)
  return (
    /(提醒|通知|告诉|叫)\s*(一下)?\s*(大家|全体|所有人|每个人)|提醒大家|通知大家|群里|集合/iu.test(text) ||
    (hasReminderKeyword && hasTimeExpression)
  )
}

export function tryParseAgentChatReminderIntent(rawText: string): AgentChatReminderIntent | null {
  const text = normalizeAgentMentionText(rawText)
  if (!text || !/(提醒|叫我|通知|告诉|闹钟|定时|timer|remind)/iu.test(text)) return null
  if (!isExplicitPersonalReminderText(text)) return null

  const match = /(\d+|半|[一二两三四五六七八九十]{1,3})\s*(秒钟|秒|分钟|分|小时|时|天)\s*(?:之后|以后|后)/u.exec(text)
  if (!match) return null

  const amount = parseChineseInteger(match[1] ?? '')
  if (!amount || amount <= 0) return null
  const unit = match[2] ?? ''
  const delayMs = delayMsForReminderAmount(amount, unit)
  if (!Number.isFinite(delayMs) || delayMs <= 0 || delayMs > MAX_AGENTCHAT_REMINDER_DELAY_MS) return null

  const after = text.slice((match.index ?? 0) + match[0].length)
  const before = text.slice(0, match.index ?? 0)
  const reminderText = (after || before)
    .replace(/^(请|帮我|麻烦)?\s*(到时|到时候)?\s*(提醒|叫|通知|告诉)\s*(一下)?\s*我?/u, '')
    .replace(/(请|帮我|麻烦)?\s*(提醒|叫|通知|告诉)\s*(一下)?\s*我?$/u, '')
    .replace(/[，。！？!,.、\s]+$/u, '')
    .replace(/^[，。！？!,.、\s]+/u, '')
    .trim()

  return {
    delayMs,
    delayLabel: match[0],
    reminderText: reminderText || '这件事',
  }
}

function cleanReminderText(text: string): string {
  return text
    .replace(/^(请|帮我|麻烦)?\s*(设置|创建|安排)?\s*(到时|到时候)?\s*(提醒|叫|通知|告诉)\s*(一下)?\s*(我|大家|全体|所有人|每个人)?/u, '')
    .replace(/^(请|帮我|麻烦)?\s*(设置|创建|安排)\s*/u, '')
    .replace(/(请|帮我|麻烦)?\s*(提醒|叫|通知|告诉)\s*(一下)?\s*(我|大家|全体|所有人|每个人)?$/u, '')
    .replace(/提醒$/u, '')
    .replace(/[，。！？!,.、\s]+$/u, '')
    .replace(/^[，。！？!,.、\s]+/u, '')
    .trim()
}

function parseReminderRelativeTime(text: string, nowMs: number): AgentChatGroupTaskIntent | null {
  const match = /(\d+|半|[一二两三四五六七八九十]{1,3})\s*(秒钟|秒|分钟|分|小时|时|天)\s*(?:之后|以后|后)/u.exec(text)
  if (!match) return null
  const amount = parseChineseInteger(match[1] ?? '')
  if (!amount || amount <= 0) return null
  const unit = match[2] ?? ''
  const delayMs = delayMsForReminderAmount(amount, unit)
  if (!Number.isFinite(delayMs) || delayMs <= 0 || delayMs > MAX_AGENTCHAT_REMINDER_DELAY_MS) return null

  const after = text.slice((match.index ?? 0) + match[0].length)
  const before = text.slice(0, match.index ?? 0)
  const reminderText = cleanReminderText(after || before) || '这件事'
  const triggerAtMs = nowMs + delayMs
  return { delayMs, delayLabel: match[0], reminderText, triggerAtMs, title: reminderText }
}

function hourValue(raw: string, period: string): number | null {
  const parsed = parseChineseInteger(raw)
  if (!parsed || parsed < 0 || parsed > 24) return null
  let hour = Math.floor(parsed)
  if (/(下午|晚上|夜里)/u.test(period) && hour < 12) hour += 12
  if (/中午/u.test(period) && hour < 11) hour += 12
  if (hour === 24) hour = 0
  return hour >= 0 && hour <= 23 ? hour : null
}

function parseTomorrowClockTime(text: string, nowMs: number): AgentChatGroupTaskIntent | null {
  const match = /明天\s*(早上|上午|中午|下午|晚上|夜里)?\s*(\d{1,2}|[一二两三四五六七八九十]{1,3})\s*(?:点|时)\s*(半)?/u.exec(text)
  if (!match) return null
  const now = new Date(nowMs)
  const hour = hourValue(match[2] ?? '', match[1] ?? '')
  if (hour === null) return null
  const triggerAt = new Date(now)
  triggerAt.setDate(now.getDate() + 1)
  triggerAt.setHours(hour, match[3] ? 30 : 0, 0, 0)
  const triggerAtMs = triggerAt.getTime()
  const delayMs = triggerAtMs - nowMs
  if (!Number.isFinite(delayMs) || delayMs <= 0 || delayMs > MAX_AGENTCHAT_REMINDER_DELAY_MS) return null
  const after = text.slice((match.index ?? 0) + match[0].length)
  const before = text.slice(0, match.index ?? 0)
  const reminderText = cleanReminderText(after || before) || '这件事'
  return { delayMs, delayLabel: match[0], reminderText, triggerAtMs, title: reminderText }
}

function parseSameDayClockTime(text: string, nowMs: number): AgentChatGroupTaskIntent | null {
  const match = /(?:今天|今日|今晚|晚上|下午|中午|上午|早上)?\s*(\d{1,2}|[一二两三四五六七八九十]{1,3})(?::|：|点|时)(\d{1,2}|半)?/u.exec(text)
  if (!match) return null
  const period = match[0].includes('下午') || match[0].includes('晚上') || match[0].includes('今晚')
    ? '下午'
    : match[0].includes('中午')
      ? '中午'
      : ''
  const hour = hourValue(match[1] ?? '', period)
  if (hour === null) return null
  const minuteRaw = match[2] ?? ''
  const minute = minuteRaw === '半' ? 30 : minuteRaw ? Number(minuteRaw) : 0
  if (!Number.isInteger(minute) || minute < 0 || minute > 59) return null

  const now = new Date(nowMs)
  const triggerAt = new Date(now)
  triggerAt.setHours(hour, minute, 0, 0)
  const triggerAtMs = triggerAt.getTime()
  const delayMs = triggerAtMs - nowMs
  if (!Number.isFinite(delayMs) || delayMs <= 0 || delayMs > MAX_AGENTCHAT_REMINDER_DELAY_MS) return null
  const after = text.slice((match.index ?? 0) + match[0].length)
  const before = text.slice(0, match.index ?? 0)
  const reminderText = cleanReminderText(after || before) || '这件事'
  return { delayMs, delayLabel: match[0].trim(), reminderText, triggerAtMs, title: reminderText }
}

export function tryParseAgentChatGroupTaskIntent(rawText: string, nowMs = Date.now()): AgentChatGroupTaskIntent | null {
  const text = normalizeAgentMentionText(rawText)
  if (!text || !isExplicitGroupReminderText(text)) return null
  return parseReminderRelativeTime(text, nowMs) || parseTomorrowClockTime(text, nowMs) || parseSameDayClockTime(text, nowMs)
}

// ══════════════════════════════════════════════════════════════════
// Cron 任务构造
// ══════════════════════════════════════════════════════════════════

export function buildAgentChatReminderCronParams(body: Record<string, unknown>, intent: AgentChatReminderIntent, nowMs = Date.now()): Record<string, unknown> | null {
  const channel = currentConversationChannel(body)
  const pushUrl = resolveAgentPushUrl()
  const token = resolveOpenClawAgentPushToken()
  if (!channel || isObsoleteAgentPushChannel(channel) || !pushUrl || !token) return null

  const triggerAt = new Date(nowMs + intent.delayMs)
  const agentRequestId = createRequestId('agentchat-reminder')
  const personalTaskId = toStringValue(body.personalTaskId)
  const reminderId = toStringValue(body.reminderId)
  const creatorUserId = toStringValue(body.userId)
  const conversationId = toStringValue(body.conversationId) || channel
  const idempotencyKey = `blazeclaw:personal-reminder:${channel}:${triggerAt.toISOString()}:${toStringValue(body.messageId) || agentRequestId}`
  const message = prefixRequesterMention(`提醒：${intent.reminderText}`, body)

  const pushPayload = {
    cmd: 'AGENT_PUSH',
    token,
    idempotencyKey,
    agentRequestId,
    channel,
    message,
    attachments: [],
    scope: 'personal',
    eventType: 'personal_task_due',
    conversationId,
    deliverTo: { type: 'conversation', channel },
    ...(personalTaskId ? { personalTaskId } : {}),
    ...(reminderId ? { reminderId } : {}),
    ...(creatorUserId ? { creatorUserId } : {}),
  }
  const pushBodyB64 = Buffer.from(JSON.stringify(pushPayload), 'utf8').toString('base64')

  return {
    name: `agentchat-reminder-${crypto.randomUUID()}`,
    description: `AgentChat reminder for ${channel}`,
    enabled: true,
    deleteAfterRun: true,
    schedule: { kind: 'at', at: triggerAt.toISOString() },
    sessionTarget: 'isolated',
    wakeMode: 'now',
    payload: {
      kind: 'agentTurn',
      message: [
        'Execute this exact PowerShell one-liner using the exec tool.',
        'Do not modify, decode, or ask questions.',
        'Reply only AGENTCHAT_PUSH_OK when done.',
        '',
        `$body = [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String('${pushBodyB64}'));`,
        `Invoke-RestMethod -Uri '${pushUrl}' -Method Post -ContentType 'application/json; charset=utf-8' -Body $body`,
      ].join('\n'),
      timeoutSeconds: 30,
      toolsAllow: ['exec'],
    },
    delivery: { mode: 'none' },
  }
}

export function buildAgentChatPushCronParams(input: {
  body: Record<string, unknown>
  intent: AgentChatReminderIntent
  triggerAtMs: number
  nowMs?: number
  postId?: string
  scope: 'personal' | 'group'
  eventType: 'personal_task_due' | 'group_task_due' | 'group_reminder_due'
  message: string
  attachments?: unknown[]
  idempotencyPrefix: string
}): Record<string, unknown> | null {
  const channel = currentConversationChannel(input.body)
  const pushUrl = resolveAgentPushUrl()
  const token = resolveOpenClawAgentPushToken()
  if (!channel || isObsoleteAgentPushChannel(channel) || !pushUrl || !token) return null
  if (typeof input.nowMs === 'number' && input.triggerAtMs <= input.nowMs + 10_000) return null

  const triggerAt = new Date(input.triggerAtMs)
  const agentRequestId = createRequestId('agentchat-reminder')
  const personalTaskId = toStringValue(input.body.personalTaskId)
  const reminderId = toStringValue(input.body.reminderId) || createRequestId('rem')
  const creatorUserId = toStringValue(input.body.userId)
  const conversationId = toStringValue(input.body.conversationId) || channel
  const idempotencyKey = `${input.idempotencyPrefix}:${channel}:${triggerAt.toISOString()}:${toStringValue(input.body.messageId) || agentRequestId}`

  const pushPayload = {
    cmd: 'AGENT_PUSH',
    token,
    idempotencyKey,
    agentRequestId,
    channel,
    message: input.message,
    attachments: input.attachments ?? [],
    scope: input.scope,
    eventType: input.eventType,
    conversationId,
    deliverTo: { type: 'conversation', channel },
    notifyMembers: input.scope === 'group',
    notification: input.scope === 'group'
      ? {
          scope: 'group_members',
          title: '群任务提醒',
          body: input.message,
          conversationId,
          postId: input.postId,
          triggerAt: input.triggerAtMs,
        }
      : undefined,
    ...(input.postId ? { postId: input.postId } : {}),
    ...(personalTaskId ? { personalTaskId } : {}),
    ...(reminderId ? { reminderId } : {}),
    ...(creatorUserId ? { creatorUserId } : {}),
  }
  const pushBodyB64 = Buffer.from(JSON.stringify(pushPayload), 'utf8').toString('base64')

  return {
    name: `agentchat-reminder-${crypto.randomUUID()}`,
    description: `AgentChat reminder for ${channel}`,
    enabled: true,
    deleteAfterRun: true,
    schedule: { kind: 'at', at: triggerAt.toISOString() },
    sessionTarget: 'isolated',
    wakeMode: 'now',
    payload: {
      kind: 'agentTurn',
      message: [
        'Execute this exact PowerShell one-liner using the exec tool.',
        'Do not modify, decode, or ask questions.',
        'Reply only AGENTCHAT_PUSH_OK when done.',
        '',
        `$body = [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String('${pushBodyB64}'));`,
        `Invoke-RestMethod -Uri '${pushUrl}' -Method Post -ContentType 'application/json; charset=utf-8' -Body $body`,
      ].join('\n'),
      timeoutSeconds: 30,
      toolsAllow: ['exec'],
    },
    delivery: { mode: 'none' },
  }
}

export function buildGroupTaskNativePostAttachment(
  body: Record<string, unknown>,
  intent: AgentChatGroupTaskIntent,
  postId: string,
): Record<string, unknown> {
  const conversationId = currentConversationChannel(body)
  const nowMs = Date.now()
  const creatorUserId = toStringValue(body.userId) || 'blazeclaw'
  const creatorName = toStringValue(body.userName || body.createdByName) || creatorUserId || '炎图AI助手'
  const title = toStringValue(intent.title).slice(0, 80) || '群任务提醒'
  const summary = `定时提醒：${intent.delayLabel}。${intent.reminderText}`
  return {
    type: 'native_post',
    postId,
    conversationId,
    title,
    summary,
    taskKind: 'task',
    actionType: 'create',
    resourceType: 'task',
    resourceUrl: '',
    deadlineAt: intent.triggerAtMs,
    status: 'published',
    visibility: 'detail_only',
    createdAt: nowMs,
    createdById: creatorUserId,
    createdByName: creatorName,
  }
}

export function buildAgentChatGroupTaskCronParams(
  body: Record<string, unknown>,
  intent: AgentChatGroupTaskIntent,
  postId: string,
  nowMs?: number,
): Record<string, unknown> | null {
  const attachment = buildGroupTaskNativePostAttachment(body, intent, postId)
  return buildAgentChatPushCronParams({
    body,
    intent,
    triggerAtMs: intent.triggerAtMs,
    nowMs,
    postId,
    scope: 'group',
    eventType: 'group_task_due',
    message: `提醒：${intent.reminderText}`,
    attachments: [attachment],
    idempotencyPrefix: 'blazeclaw:group-task',
  })
}

// ══════════════════════════════════════════════════════════════════
// 设备身份
// ══════════════════════════════════════════════════════════════════

export interface DeviceIdentity {
  version: number
  deviceId: string
  publicKeyPem: string
  privateKeyPem: string
}

export function derivePublicKeyRaw(publicKeyPem: string): Buffer {
  const spki = crypto.createPublicKey(publicKeyPem).export({ type: 'spki', format: 'der' })
  if (
    spki.length === ED25519_SPKI_PREFIX.length + 32 &&
    spki.subarray(0, ED25519_SPKI_PREFIX.length).equals(ED25519_SPKI_PREFIX)
  ) {
    return spki.subarray(ED25519_SPKI_PREFIX.length)
  }
  return spki
}

export function publicKeyRawBase64UrlFromPem(publicKeyPem: string): string {
  return base64UrlEncode(derivePublicKeyRaw(publicKeyPem))
}

export function signDevicePayload(privateKeyPem: string, payload: string): string {
  const key = crypto.createPrivateKey(privateKeyPem)
  return base64UrlEncode(crypto.sign(null, Buffer.from(payload, 'utf8'), key))
}

export function loadDeviceIdentity(): DeviceIdentity {
  const identityPath = statePath('identity', 'device.json')
  const parsed = readJson(identityPath)
  if (
    parsed?.version !== 1 ||
    typeof parsed.deviceId !== 'string' ||
    typeof parsed.publicKeyPem !== 'string' ||
    typeof parsed.privateKeyPem !== 'string'
  ) {
    throw new Error('OpenClaw device identity is missing or invalid.')
  }
  return parsed
}

export function loadStoredDeviceToken(deviceId: string, role: string): string | undefined {
  try {
    const parsed = readJson(statePath('identity', 'device-auth.json'))
    if (parsed?.version !== 1 || parsed.deviceId !== deviceId) return undefined
    const token = parsed.tokens?.[role]?.token
    return typeof token === 'string' && token.trim() ? token.trim() : undefined
  } catch {
    return undefined
  }
}

export interface DeviceAuthParams {
  deviceId: string
  clientId: string
  clientMode: string
  role: string
  scopes: string[]
  signedAtMs: number
  token?: string
  nonce: string
  platform?: string
  deviceFamily?: string
}

export function buildDeviceAuthPayload(params: DeviceAuthParams): string {
  return [
    'v3',
    params.deviceId,
    params.clientId,
    params.clientMode,
    params.role,
    params.scopes.join(','),
    String(params.signedAtMs),
    params.token ?? '',
    params.nonce,
    params.platform ?? '',
    params.deviceFamily ?? '',
  ].join('|')
}

// ══════════════════════════════════════════════════════════════════
// AI 响应解析
// ══════════════════════════════════════════════════════════════════

export function parseAssistantText(message: unknown): string {
  if (typeof message === 'string') return message.trim()
  if (!message || typeof message !== 'object') return ''
  const record = message as Record<string, unknown>
  if (typeof record.text === 'string') return record.text.trim()
  if (typeof record.content === 'string') return record.content.trim()
  if (Array.isArray(record.content)) {
    return record.content
      .map((entry: unknown) => {
        if (typeof entry === 'string') return entry
        if (entry && typeof entry === 'object') {
          const block = entry as Record<string, unknown>
          if (toStringValue(block.type) === 'tool_use') return ''
          return flattenOpenClawContentText(block)
        }
        return ''
      })
      .filter(Boolean)
      .join('\n')
      .trim()
  }
  return ''
}

function flattenOpenClawContentText(value: unknown): string {
  if (typeof value === 'string') return value.trim()
  if (Array.isArray(value)) {
    return value
      .map((entry) => flattenOpenClawContentText(entry))
      .filter(Boolean)
      .join('\n')
      .trim()
  }
  if (!value || typeof value !== 'object') return ''
  const record = value as Record<string, unknown>
  const directValue = [record.text, record.content, record.result, record.summary]
    .find((item) => typeof item === 'string' || typeof item === 'number')
  const directText = toStringValue(directValue)
  const nestedText = directText ? '' : flattenOpenClawContentText(record.content)
  const url = toStringValue(
    record.url ||
    record.openUrl ||
    record.open_url ||
    record.href ||
    record.src ||
    record.imageUrl ||
    record.image_url ||
    record.target_url ||
    record.targetUrl ||
    record.cdn_url ||
    record.cdnUrl,
  )
  const title = toStringValue(record.title || record.name)
  return [
    directText,
    nestedText,
    url ? `${title ? `${title} ` : ''}${url}` : '',
  ].filter(Boolean).join('\n').trim()
}

export interface ParsedContentBlock {
  type: 'text' | 'tool_use' | 'tool_result'
  text?: string
  id?: string
  name?: string
  input?: Record<string, unknown>
  toolUseId?: string
  content?: string
}

export function parseContentBlock(block: unknown): ParsedContentBlock | null {
  if (!block || typeof block !== 'object') return null
  const record = block as Record<string, unknown>
  const type = toStringValue(record.type)
  if (type === 'text') return { type: 'text', text: toStringValue(record.text) }
  if (type === 'tool_use') {
    return {
      type: 'tool_use',
      id: toStringValue(record.id),
      name: toStringValue(record.name),
      input: record.input && typeof record.input === 'object' ? record.input as Record<string, unknown> : {},
    }
  }
  if (type === 'tool_result') {
    return {
      type: 'tool_result',
      toolUseId: toStringValue(record.tool_use_id),
      content: flattenOpenClawContentText(record.content),
    }
  }
  return null
}

export function parseMessageContentBlocks(message: unknown): ParsedContentBlock[] {
  if (!message || typeof message !== 'object') return []
  const record = message as Record<string, unknown>
  if (Array.isArray(record.content)) {
    return record.content.map(parseContentBlock).filter((b): b is ParsedContentBlock => b !== null)
  }
  if (typeof record.text === 'string') {
    return [{ type: 'text', text: record.text }]
  }
  return []
}

// ══════════════════════════════════════════════════════════════════
// Delta Tracker
// ══════════════════════════════════════════════════════════════════

export interface OpenClawDeltaTextTracker {
  lastSnapshotText: string
}

export function createOpenClawDeltaTextTracker(): (payload: Record<string, unknown>) => string {
  const tracker: OpenClawDeltaTextTracker = { lastSnapshotText: '' }
  return (payload) => {
    const delta = toStringValue(payload.delta)
    const snapshotText = parseAssistantText(payload.message)

    if (delta) {
      tracker.lastSnapshotText = snapshotText || `${tracker.lastSnapshotText}${delta}`
      return delta
    }

    if (!snapshotText || snapshotText === tracker.lastSnapshotText) return ''

    if (!tracker.lastSnapshotText) {
      tracker.lastSnapshotText = snapshotText
      return snapshotText
    }

    if (snapshotText.startsWith(tracker.lastSnapshotText)) {
      const nextDelta = snapshotText.slice(tracker.lastSnapshotText.length)
      tracker.lastSnapshotText = snapshotText
      return nextDelta
    }

    tracker.lastSnapshotText = snapshotText
    return ''
  }
}

// ══════════════════════════════════════════════════════════════════
// 工具调用
// ══════════════════════════════════════════════════════════════════

export interface OpenClawBridgeToolCall {
  id: string
  name: string
  input: Record<string, unknown>
}

export function extractToolCalls(payload: Record<string, unknown>): OpenClawBridgeToolCall[] {
  return parseMessageContentBlocks(payload.message)
    .filter((b) => b.type === 'tool_use')
    .map((tool) => ({
      id: tool.id || createRequestId('tool'),
      name: tool.name ?? '',
      input: tool.input ?? {},
    }))
    .filter((tool) => tool.name)
}

export function extractToolResults(payload: Record<string, unknown>): ParsedContentBlock[] {
  return parseMessageContentBlocks(payload.message).filter((b) => b.type === 'tool_result')
}

// ══════════════════════════════════════════════════════════════════
// OpenClaw Bridge WebSocket 客户端
// ══════════════════════════════════════════════════════════════════

export interface BridgeClientOptions {
  url: string
  timeoutMs: number
  pollTimeoutMs: number
}

interface PendingRequest {
  resolve: (response: { ok: boolean; error?: { message?: string; details?: { code?: string } }; payload?: unknown }) => void
  reject: (error: Error) => void
  timer: ReturnType<typeof setTimeout>
}

export class OpenClawBridgeClient {
  private readonly url: string
  private readonly timeoutMs: number
  readonly pollTimeoutMs: number
  private socket: WebSocket | null = null
  private readonly pending = new Map<string, PendingRequest>()
  private readonly listeners = new Set<(frame: { type: string; event: string; payload?: Record<string, unknown> }) => void>()

  constructor(options: BridgeClientOptions) {
    this.url = options.url
    this.timeoutMs = options.timeoutMs
    this.pollTimeoutMs = options.pollTimeoutMs
  }

  async connect(): Promise<void> {
    const identity = loadDeviceIdentity()
    const role = 'operator'
    const clientId = 'cli'
    const clientMode = 'cli'
    const scopes = DEFAULT_SCOPES
    const deviceToken = loadStoredDeviceToken(identity.deviceId, role)

    let protocolVersion = resolveProtocolVersion()

    while (true) {
      try {
        await this.performConnect({ identity, role, clientId, clientMode, scopes, deviceToken, protocolVersion })
        return
      } catch (error) {
        const expectedVersion = tryExtractProtocolMismatchVersion(error)
        if (expectedVersion && expectedVersion !== protocolVersion) {
          console.log(`[openclaw-bridge-client] protocol mismatch, retrying with version ${expectedVersion}`)
          protocolVersion = expectedVersion
          continue
        }
        throw error
      }
    }
  }

  private performConnect(params: {
    identity: DeviceIdentity
    role: string
    clientId: string
    clientMode: string
    scopes: string[]
    deviceToken: string | undefined
    protocolVersion: number
  }): Promise<void> {
    const { identity, role, clientId, clientMode, scopes, deviceToken, protocolVersion } = params

    return new Promise<void>((resolve, reject) => {
      const socket = new WebSocket(this.url)
      const timer = setTimeout(() => {
        socket.close()
        reject(new Error('OpenClaw gateway connection timed out.'))
      }, this.timeoutMs)

      const fail = (error: unknown) => {
        clearTimeout(timer)
        reject(error instanceof Error ? error : new Error(String(error)))
        socket.close()
      }

      socket.addEventListener('open', () => {
        this.socket = socket
      })
      socket.addEventListener('error', () => fail(new Error('OpenClaw gateway connection failed.')))
      socket.addEventListener('close', () => {
        this.rejectAll(new Error('OpenClaw gateway connection closed.'))
        this.socket = null
      })
      socket.addEventListener('message', (event: MessageEvent) => {
        const frame = this.handleMessage(event.data as string)
        if (frame?.type !== 'event' || frame.event !== 'connect.challenge') return
        const nonce = toStringValue(frame.payload?.nonce)
        if (!nonce) {
          fail(new Error('OpenClaw gateway connect challenge missing nonce.'))
          return
        }
        const signedAtMs = Date.now()
        const payload = buildDeviceAuthPayload({
          deviceId: identity.deviceId,
          clientId,
          clientMode,
          role,
          scopes,
          signedAtMs,
          token: deviceToken,
          nonce,
          platform: process.platform,
        })
        this.requestOnSocket(socket, 'connect', {
          minProtocol: protocolVersion,
          maxProtocol: protocolVersion,
          client: {
            id: clientId,
            displayName: 'agent-chat-android-bridge',
            version: 'agent-chat',
            platform: process.platform,
            mode: clientMode,
            instanceId: createRequestId('instance'),
          },
          caps: ['tool-events'],
          auth: deviceToken ? { token: deviceToken, deviceToken } : undefined,
          role,
          scopes,
          device: {
            id: identity.deviceId,
            publicKey: publicKeyRawBase64UrlFromPem(identity.publicKeyPem),
            signature: signDevicePayload(identity.privateKeyPem, payload),
            signedAt: signedAtMs,
            nonce,
          },
        })
          .then(() => {
            clearTimeout(timer)
            resolve()
          })
          .catch(fail)
      })
    })
  }

  close(): void {
    this.rejectAll(new Error('OpenClaw bridge client closed.'))
    this.socket?.close()
    this.socket = null
  }

  isConnected(): boolean {
    return this.socket?.readyState === WebSocket.OPEN
  }

  request(method: string, params: Record<string, unknown>, timeoutMs?: number): Promise<unknown> {
    const socket = this.socket
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      return Promise.reject(new Error('OpenClaw gateway is not connected.'))
    }
    return this.requestOnSocket(socket, method, params, timeoutMs)
  }

  addEventListener(listener: (frame: { type: string; event: string; payload?: Record<string, unknown> }) => void): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  private requestOnSocket(
    socket: WebSocket,
    method: string,
    params: Record<string, unknown>,
    timeoutMs = this.timeoutMs,
  ): Promise<unknown> {
    if (socket.readyState !== WebSocket.OPEN) {
      return Promise.reject(new Error('OpenClaw gateway is not connected.'))
    }
    const id = createRequestId()
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id)
        reject(new Error(`OpenClaw method timed out: ${method}`))
      }, timeoutMs)

      this.pending.set(id, {
        resolve: (response) => {
          if (!response.ok) {
            const detailCode = response.error?.details?.code
            const message = response.error?.message || detailCode || `OpenClaw method failed: ${method}`
            const err = new Error(detailCode ? `${message} (${detailCode})` : message)
            if (response.error?.details) {
              ;(err as unknown as Record<string, unknown>).details = response.error.details
            }
            reject(err)
            return
          }
          resolve(response.payload)
        },
        reject,
        timer,
      })

      socket.send(JSON.stringify({
        type: 'req',
        id,
        method,
        params,
      }))
    })
  }

  private handleMessage(raw: string): { type: string; event: string; payload?: Record<string, unknown>; id?: string; ok?: boolean; error?: { message?: string; details?: { code?: string } } } | null {
    let frame: Record<string, unknown>
    try {
      frame = JSON.parse(raw)
    } catch {
      return null
    }
    if (frame.type === 'res' && frame.id) {
      const pending = this.pending.get(String(frame.id))
      if (pending) {
        clearTimeout(pending.timer)
        this.pending.delete(String(frame.id))
        pending.resolve(frame as unknown as { ok: boolean; error?: { message?: string; details?: { code?: string } }; payload?: unknown })
      }
      return null
    }
    if (frame.type === 'event') {
      const eventFrame = frame as unknown as { type: string; event: string; payload?: Record<string, unknown> }
      this.listeners.forEach((listener) => listener(eventFrame))
      return eventFrame
    }
    return null
  }

  private rejectAll(error: Error): void {
    this.pending.forEach((entry) => {
      clearTimeout(entry.timer)
      entry.reject(error)
    })
    this.pending.clear()
  }
}

export class OpenClawBridgeClientManager {
  private client: OpenClawBridgeClient | null = null
  private connectPromise: Promise<OpenClawBridgeClient> | null = null

  async getClient(options: BridgeClientOptions): Promise<OpenClawBridgeClient> {
    if (this.client?.isConnected()) return this.client
    if (this.connectPromise) return this.connectPromise

    const client = new OpenClawBridgeClient(options)
    this.connectPromise = client.connect()
      .then(() => {
        this.client = client
        console.log(`[openclaw-bridge-client] connected gateway ${options.url}`)
        return client
      })
      .catch((error) => {
        client.close()
        if (this.client === client) this.client = null
        throw error
      })
      .finally(() => {
        this.connectPromise = null
      })

    return this.connectPromise
  }

  invalidate(client: OpenClawBridgeClient): void {
    if (this.client !== client) return
    this.client.close()
    this.client = null
  }
}

export const bridgeClientManager = new OpenClawBridgeClientManager()

// ══════════════════════════════════════════════════════════════════
// Chat 事件处理
// ══════════════════════════════════════════════════════════════════

export interface TerminalChatResult {
  text: string
  toolCalls: OpenClawBridgeToolCall[]
}

function readRecentTrajectoryResult(runId: string, startedAt: number): string | null {
  const sessionsDir = statePath('agents', 'main', 'sessions')
  let files: fs.Dirent[]
  try {
    files = fs.readdirSync(sessionsDir, { withFileTypes: true })
  } catch {
    return null
  }

  const candidates = files
    .filter((file) => file.isFile() && file.name.endsWith('.trajectory.jsonl'))
    .map((file) => {
      const fullPath = path.join(sessionsDir, file.name)
      try {
        return { fullPath, mtimeMs: fs.statSync(fullPath).mtimeMs }
      } catch {
        return null
      }
    })
    .filter((file): file is { fullPath: string; mtimeMs: number } => Boolean(file))
    .filter((file) => file.mtimeMs >= startedAt - 1000)
    .sort((a, b) => b.mtimeMs - a.mtimeMs)
    .slice(0, 8)

  for (const candidate of candidates) {
    let raw = ''
    try {
      raw = fs.readFileSync(candidate.fullPath, 'utf8')
    } catch {
      continue
    }
    if (!raw.includes(runId)) continue

    const lines = raw.trim().split(/\r?\n/).reverse()
    for (const line of lines) {
      if (!line.includes(runId) || !line.includes('trace.artifacts')) continue
      try {
        const parsed = JSON.parse(line) as Record<string, unknown>
        const data = parsed.data as Record<string, unknown> | undefined
        const assistantTexts = Array.isArray(data?.assistantTexts) ? data.assistantTexts : []
        const finalText = assistantTexts
          .map((value) => toStringValue(value))
          .filter(Boolean)
          .at(-1)
        if (finalText) return finalText
      } catch {
        continue
      }
    }
  }

  return null
}

export function isLowInformationOpenClawFinalText(text: string): boolean {
  const normalized = toStringValue(text).replace(/\s+/g, '')
  if (!normalized) return true
  return /^(已处理完成[，,。]?请查看当前结果[。.]?|已处理完成[。.]?|处理完成[。.]?|完成[。.]?|OpenClawfinishedwithnotext[.]?)$/iu.test(normalized)
}

export function resolveOpenClawTerminalVisibleText(input: {
  runId: string
  startedAt: number
  rawText: string
  fallback: string
}): string {
  const visibleText = sanitizeOpenClawVisibleText(input.rawText, input.fallback)
  const visibleIsLowInformation = isLowInformationOpenClawFinalText(visibleText)
  const visibleHasHttpUrl = /https?:\/\//im.test(visibleText)
  if (!visibleIsLowInformation && visibleHasHttpUrl) return visibleText

  const trajectoryText = readRecentTrajectoryResult(input.runId, input.startedAt)
  if (!trajectoryText) return visibleText

  const recoveredText = sanitizeOpenClawVisibleText(trajectoryText, input.fallback)
  const recoveredHasHttpUrl = /https?:\/\//im.test(recoveredText)
  if ((visibleIsLowInformation && !isLowInformationOpenClawFinalText(recoveredText)) || recoveredHasHttpUrl) {
    console.log(`[openclaw-chat] recovered final text from trajectory runId=${input.runId.slice(0, 40)} msgLen=${recoveredText.length}`)
    return recoveredText
  }
  return visibleText
}

export function waitForTerminalChatEvent(
  client: OpenClawBridgeClient,
  runId: string,
  timeoutMs: number,
): Promise<TerminalChatResult> {
  return new Promise((resolve, reject) => {
    const startedAt = Date.now()
    const toolCalls: OpenClawBridgeToolCall[] = []
    let lastMessageText = ''
    let toolResultTexts: string[] = []
    const timer = setTimeout(() => {
      unsubscribe()
      const fallbackText = readRecentTrajectoryResult(runId, startedAt)
      if (fallbackText) {
        const visibleText = sanitizeOpenClawVisibleText(
          fallbackText,
          '已处理完成，请查看当前结果。',
        )
        console.log(`[openclaw-chat] trajectory fallback runId=${runId.slice(0, 40)} msgLen=${visibleText.length}`)
        resolve({ text: visibleText, toolCalls })
        return
      }
      const elapsed = Date.now() - startedAt
      reject(new Error(`No terminal event received for runId ${runId} after ${elapsed}ms.`))
    }, timeoutMs)
    const unsubscribe = client.addEventListener((frame) => {
      if (frame.event !== 'chat') return
      const payload = frame.payload ?? {}
      if (toStringValue(payload.runId) !== runId) return
      const state = toStringValue(payload.state)
      console.log(`[openclaw-chat] event state="${state}" runId=${runId.slice(0, 40)}`)
      toolCalls.push(...extractToolCalls(payload))
      // Track the latest message text and collect tool result content
      const parsed = parseAssistantText(payload.message)
      if (parsed) lastMessageText = parsed
      for (const result of extractToolResults(payload)) {
        if (result.content) toolResultTexts.push(result.content)
      }
      if (state === 'final') {
        // Tool results are the canonical visible output when present.
        const combined = toolResultTexts.length
          ? toolResultTexts.join('\n')
          : lastMessageText
        console.log(`[openclaw-chat] final msgLen=${lastMessageText.length} toolResults=${toolResultTexts.length}`)
        const visibleText = resolveOpenClawTerminalVisibleText({
          runId,
          startedAt,
          rawText: combined || 'OpenClaw finished with no text.',
          fallback: '已处理完成，请查看当前结果。',
        })
        clearTimeout(timer)
        unsubscribe()
        resolve({ text: visibleText, toolCalls })
      }
      if (state === 'aborted') {
        clearTimeout(timer)
        unsubscribe()
        reject(new Error('OpenClaw chat runtime aborted.'))
      }
      if (state === 'error') {
        clearTimeout(timer)
        unsubscribe()
        reject(new Error(toStringValue(payload.errorMessage) || 'OpenClaw chat runtime failed.'))
      }
    })
  })
}

// ══════════════════════════════════════════════════════════════════
// HBPC 协议 (TCP 层)
// ══════════════════════════════════════════════════════════════════

// 全局序列号
let hbpcSeq = 0

export function buildHBPCHeader(type: number, payloadByteLength: number, sessionId = 0n): Buffer {
  const buf = Buffer.alloc(HBPC_HEADER_SIZE, 0)
  HBPC_PROTO_MAGIC.copy(buf, 0)
  buf.writeUInt8(HBPC_PROTO_VERSION, 4)
  buf.writeUInt8(type & 0xff, 5)
  buf.writeUInt8(0, 6)
  buf.writeUInt8(0, 7)
  buf.writeUInt32BE(++hbpcSeq >>> 0, 8)
  buf.writeUInt32BE(payloadByteLength >>> 0, 12)
  buf.writeBigUInt64BE(sessionId, 16)
  buf.writeBigUInt64BE(BigInt(Date.now()), 24)
  return buf
}

export function parseHBPCHeader(buf: Buffer) {
  if (buf.length < HBPC_HEADER_SIZE) return null
  return {
    magic: buf.subarray(0, 4).toString('ascii'),
    version: buf.readUInt8(4),
    type: buf.readUInt8(5),
    payloadLen: buf.readUInt32BE(12),
    sessionId: buf.readBigUInt64BE(16),
  }
}

export function readExactlyNBytes(socket: net.Socket, size: number): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = []
    let total = 0

    const cleanup = () => {
      socket.off('readable', tryRead)
      socket.off('error', onError)
      socket.off('end', onEnd)
      socket.off('close', onClose)
    }
    const onError = (error: Error) => {
      cleanup()
      reject(error)
    }
    const onEnd = () => {
      cleanup()
      reject(new Error('socket closed before reading full payload'))
    }
    const onClose = () => {
      if (total < size) onEnd()
    }
    function tryRead() {
      let chunk: Buffer | null
      while ((chunk = socket.read(size - total) as Buffer | null) !== null) {
        chunks.push(chunk)
        total += chunk.length
        if (total >= size) {
          cleanup()
          resolve(Buffer.concat(chunks, size))
          return
        }
      }
    }

    socket.on('readable', tryRead)
    socket.on('error', onError)
    socket.on('end', onEnd)
    socket.on('close', onClose)
    tryRead()
  })
}

export async function readHBPCResponse(socket: net.Socket): Promise<Record<string, unknown>> {
  const headerBuffer = await readExactlyNBytes(socket, HBPC_HEADER_SIZE)
  const header = parseHBPCHeader(headerBuffer)
  if (!header || header.magic !== 'HBPC') throw new Error(`invalid response magic=${header?.magic ?? 'null'}`)
  if (header.version !== HBPC_PROTO_VERSION) throw new Error(`unsupported response version=${header.version}`)
  if (header.type !== HBPC_IRC_MESSAGE_RESP) throw new Error(`unexpected response type=${header.type}`)
  if (header.payloadLen > HBPC_MAX_PAYLOAD_SIZE) throw new Error('invalid response payload_len')

  const payloadBuffer = header.payloadLen ? await readExactlyNBytes(socket, header.payloadLen) : Buffer.alloc(0)
  const text = payloadBuffer.toString('utf8')
  try {
    return text ? JSON.parse(text) as Record<string, unknown> : {}
  } catch {
    throw new Error('response is not valid JSON')
  }
}

// ══════════════════════════════════════════════════════════════════
// Chat-server TCP 通信
// ══════════════════════════════════════════════════════════════════

export interface AgentPushPayload {
  cmd: 'AGENT_PUSH'
  token: string
  channel: string
  message?: string
  typing?: boolean
  idempotencyKey?: string
  attachments?: unknown[]
  agentRequestId?: string
  scope?: 'personal' | 'group'
  eventType?: string
  postId?: string
  personalTaskId?: string
  reminderId?: string
  creatorUserId?: string
  conversationId?: string
  deliverTo?: Record<string, unknown>
  notifyMembers?: boolean
  notification?: Record<string, unknown>
}

function withTimeout<T>(promise: Promise<T>, timeoutMs: number, message: string): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(message)), timeoutMs)
    promise.then(
      (value) => { clearTimeout(timer); resolve(value) },
      (error) => { clearTimeout(timer); reject(error) },
    )
  })
}

export async function sendChatServerRequest(
  payload: Record<string, unknown>,
  options?: { host?: string; port?: number; timeoutMs?: number },
): Promise<Record<string, unknown>> {
  const raw = Buffer.from(JSON.stringify(payload), 'utf8')
  if (raw.length > HBPC_MAX_PAYLOAD_SIZE) throw new Error('payload too large')

  const host = options?.host || resolveChatHost()
  const port = options?.port || resolveChatPort()
  const timeoutMs = options?.timeoutMs || resolvePushTimeoutMs()
  const socket = net.connect({ host, port })

  try {
    await new Promise<void>((resolve, reject) => {
      const timer = setTimeout(() => {
        socket.destroy()
        reject(new Error(`chat server connect timeout after ${timeoutMs}ms (${host}:${port})`))
      }, timeoutMs)
      socket.once('connect', () => { clearTimeout(timer); resolve() })
      socket.once('error', (error) => { clearTimeout(timer); reject(error) })
    })

    const header = buildHBPCHeader(HBPC_IRC_MESSAGE_REQ, raw.length, 0n)
    await new Promise<void>((resolve, reject) => {
      socket.write(Buffer.concat([header, raw]), (error) => (error ? reject(error) : resolve()))
    })
    return await withTimeout(readHBPCResponse(socket), timeoutMs, `chat server response timeout after ${timeoutMs}ms`)
  } finally {
    socket.destroy()
  }
}

export async function sendAgentPushToChatServer(
  payload: AgentPushPayload,
  options?: { host?: string; port?: number; timeoutMs?: number },
): Promise<Record<string, unknown>> {
  return sendChatServerRequest(payload as unknown as Record<string, unknown>, options)
}
