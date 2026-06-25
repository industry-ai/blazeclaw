/**
 * OpenClaw Agent 桥接服务。
 *
 * 职责：
 *   1. 接收前端 HTTP/SSE 请求，通过 WebSocket 转发到本机 OpenClaw Gateway
 *   2. 解析用户消息中的提醒意图（"10分钟后提醒我喝水"），通过 cron.add 创建定时任务
 *   3. 在每次对话中注入 cron context，教 AI 如何创建 AgentChat 定时提醒
 *
 * 提醒链路：
 *   用户发"10分钟后提醒我" → bridge 识别意图 → cron.add 创建 one-shot 定时任务
 *   → 定时触发 → agentTurn exec PowerShell → POST /api/openclaw-agent-push
 *   → push bridge → chat-server 房间广播 → 客户端收到提醒
 */
import crypto from 'node:crypto'
import type { IncomingMessage, ServerResponse } from 'node:http'
import {
  toStringValue,
  createRequestId,
  resolveTimeout,
  isPersonalWorkspaceChannel,
  currentConversationChannel,
  requesterMentionFromBody,
  prefixRequesterMention,
  sanitizeOpenClawVisibleText,
  sanitizeOpenClawDeltaText,
  isExplicitGroupReminderText,
  tryParseAgentChatReminderIntent,
  tryParseAgentChatGroupTaskIntent,
  buildAgentChatReminderCronParams,
  buildGroupTaskNativePostAttachment,
  buildAgentChatGroupTaskCronParams,
  resolveAgentPushUrl,
  resolveOpenClawAgentPushToken,
  resolveGatewayUrl,
  isObsoleteAgentPushChannel,
  conversationTaskScope,
  parseAssistantText,
  createOpenClawDeltaTextTracker,
  resolveOpenClawTerminalVisibleText,
  extractToolCalls,
  extractToolResults,
  OpenClawBridgeClient,
  bridgeClientManager,
  waitForTerminalChatEvent,
  sendChatServerRequest,
  MAX_AGENTCHAT_REMINDER_DELAY_MS,
  type AgentChatReminderIntent,
  type AgentChatGroupTaskIntent,
  type OpenClawBridgeToolCall,
} from './openclaw-shared'

// ── 重新导出 (保持 API 兼容) ──
export {
  type AgentChatReminderIntent,
  type AgentChatGroupTaskIntent,
  tryParseAgentChatReminderIntent,
  tryParseAgentChatGroupTaskIntent,
  buildAgentChatReminderCronParams,
  buildAgentChatGroupTaskCronParams,
  requesterMentionFromBody,
  sanitizeOpenClawVisibleText,
  createOpenClawDeltaTextTracker,
}

// ── 默认配置 ──
const DEFAULT_TIMEOUT_MS = 10000
const DEFAULT_POLL_TIMEOUT_MS = 600000

// ── Cron Context（注入到 AI 对话中的定时提醒指南）──

function buildAgentChatCronContext(body: Record<string, unknown>): string {
  const pushUrl = resolveAgentPushUrl()
  const pushToken = resolveOpenClawAgentPushToken()
  const channel = currentConversationChannel(body)
  if (!pushUrl || !pushToken || !channel || isObsoleteAgentPushChannel(channel)) {
    console.log(`[openclaw-agent-bridge] cron context skipped channel=${channel || '-'} pushUrl=${pushUrl || '-'} tokenConfigured=${Boolean(pushToken)}`)
    return ''
  }
  const taskScope = conversationTaskScope(channel)
  const eventType = taskScope === 'personal' ? 'personal_task_due' : 'group_reminder_due'
  console.log(`[openclaw-agent-bridge] cron context enabled channel=${channel} pushUrl=${pushUrl}`)

  return [
    '',
    'AgentChat integration context:',
    '- The current chat channel is the exact AgentChat server room key below. Use it unchanged.',
    `- channel: ${channel}`,
    `- conversationId: ${toStringValue(body.conversationId) || channel}`,
    `- conversationTaskScope: ${taskScope}`,
    `- defaultEventType: ${eventType}`,
    '- When the user asks for a future reminder, alarm, timer, or scheduled message, create an OpenClaw cron job instead of saying reminders are unavailable.',
    '- Use a one-shot cron schedule (kind="at") for relative times such as "in 10 minutes" and absolute times such as "Sunday at 8 PM" or "tomorrow morning at 9".',
    '- The cron job must deliver the reminder to AgentChat by POSTing to the push bridge at the scheduled time.',
    '- Task ownership is determined by the current AgentChat conversation, not by a fixed keyword list.',
    '- If the user explicitly asks for a reminder only for the requester, set scope="personal", eventType="personal_task_due", and send it to the current channel mentioning creatorUserId when available.',
    '- Otherwise, if conversationTaskScope is "personal", set scope="personal" and eventType="personal_task_due".',
    '- Otherwise, if conversationTaskScope is "group", set scope="group", eventType="group_reminder_due" unless an existing group task/post id is provided, then use eventType="group_task_due" and include postId.',
    '- Group reminders must be sent to the conversation channel so the server can deliver them to that group.',
    `- pushUrl: ${pushUrl}`,
    `- token: ${pushToken}`,
    '- CRITICAL: delivery.mode="webhook" is NOT supported. Do NOT use it. Use delivery: {"mode":"none"}.',
    '- Instead, set payload.kind="agentTurn" with toolsAllow=["exec"]. The agent turn message must be a single PowerShell one-liner that POSTs the AGENT_PUSH JSON to pushUrl using base64-decoded body.',
    '- Use sessionTarget="isolated" and wakeMode="now".',
    '- The agent turn message format (copy this exactly, filling in the placeholders):',
    '  $body = [System.Text.Encoding]::UTF8.GetString([System.Convert]::FromBase64String(\'<BASE64_OF_PUSH_JSON>\')); Invoke-RestMethod -Uri \'<pushUrl>\' -Method Post -ContentType \'application/json; charset=utf-8\' -Body $body',
    '- Encode the push JSON as base64 so no escaping issues occur. The push JSON schema:',
    '  {"cmd":"AGENT_PUSH","token":"<token>","idempotencyKey":"openclaw:reminder:<channel>:<triggerAt>:<requestId>","channel":"<channel>","message":"提醒：<reminder text>","attachments":[],"scope":"personal","eventType":"personal_task_due","personalTaskId":"<optional frontend personalTaskId>","reminderId":"<optional reminderId>","creatorUserId":"<optional userId>","deliverTo":{"type":"conversation","channel":"<channel>"}}',
    '  {"cmd":"AGENT_PUSH","token":"<token>","idempotencyKey":"openclaw:group-reminder:<channel>:<triggerAt>:<requestId>","channel":"<channel>","message":"提醒：<group reminder text>","attachments":[],"scope":"group","eventType":"group_reminder_due","postId":"<optional group post/task id>","reminderId":"<optional reminderId>","creatorUserId":"<optional userId>","conversationId":"<conversationId>","deliverTo":{"type":"conversation","channel":"<channel>"}}',
    '- The idempotencyKey format: openclaw:reminder:<channel>:<triggerAt ISO string>:<random UUID>',
    '- Set timeoutSeconds=30 in the payload.',
    '- Set deleteAfterRun=true so the cron job self-deletes after firing.',
    '- Do not use #group-posts-demo or #personal-workspace. If the channel is missing, do not create an AgentChat push cron job.',
    '- After the cron job is created successfully, reply briefly that the reminder has been scheduled.',
    '- User-visible replies must be Simplified Chinese only. Never mention base64, PowerShell, exec, webhook, delivery mode, cron internals, or debugging steps.',
  ].join('\n')
}

// ── Card Files（从 COS 获取卡片模板并注入 AI 上下文）──

interface CardFileEntry {
  file_key: string
  file_type: 'json' | 'js' | 'css' | 'html'
  cdn_url: string | null
  content: unknown
}

interface CardFilesResponse {
  status: string
  event: string
  card_path: string
  file_count: number
  files: CardFileEntry[]
}

interface CardMetadata {
  title: string
  summary: string
  target_url: string
  template: string
  card_type: string
}

interface CardPathEntry {
  businessType: string
  cardPath: string
}

const cardFilesCache = new Map<string, { ts: number; context: string }>()
const CARD_FILES_CACHE_TTL_MS = 60_000

function resolveCardPaths(): CardPathEntry[] {
  const raw = toStringValue(process.env.AGENTCHAT_CARD_PATHS || '')
  if (!raw) return []
  return raw.split(',').flatMap((s) => {
    const trimmed = s.trim()
    if (!trimmed) return []
    const colonIdx = trimmed.indexOf(':')
    if (colonIdx > 0) {
      const businessType = trimmed.slice(0, colonIdx).trim()
      const cardPath = trimmed.slice(colonIdx + 1).trim()
      if (!businessType || !cardPath) return []
      return [{ businessType, cardPath }]
    }
    const segments = trimmed.replace(/\/+$/, '').split('/')
    const lastSegment = segments[segments.length - 1] || 'general'
    const businessType = lastSegment.replace(/-card$/i, '')
    return [{ businessType, cardPath: trimmed }]
  })
}

async function fetchCardFilesFromChatServer(cardPath: string): Promise<CardFilesResponse | null> {
  try {
    const response = await sendChatServerRequest({ cmd: 'GET_CARD_FILES', card_path: cardPath })
    if (toStringValue(response.status) !== 'ok') {
      console.log(`[openclaw-agent-bridge] GET_CARD_FILES failed for ${cardPath}: status=${toStringValue(response.status)}`)
      return null
    }
    return {
      status: toStringValue(response.status),
      event: toStringValue(response.event),
      card_path: toStringValue(response.card_path),
      file_count: Number(response.file_count ?? 0),
      files: Array.isArray(response.files) ? response.files as CardFileEntry[] : [],
    }
  } catch (error) {
    console.log(`[openclaw-agent-bridge] GET_CARD_FILES error for ${cardPath}: ${error instanceof Error ? error.message : String(error)}`)
    return null
  }
}

export function extractCardMetadata(response: CardFilesResponse): { cards: CardMetadata[]; indexHtmlCdnUrl: string | null } {
  const cards: CardMetadata[] = []
  let indexHtmlCdnUrl: string | null = null

  for (const file of response.files) {
    if (file.file_type === 'html' && file.file_key.endsWith('index.html')) {
      indexHtmlCdnUrl = toStringValue(file.cdn_url) || null
      continue
    }
    if (file.file_type !== 'json') continue

    const content = file.content
    if (!content || typeof content !== 'object') continue

    const record = content as Record<string, unknown>
    const title = toStringValue(record.title)
    if (!title) continue

    cards.push({
      title,
      summary: toStringValue(record.summary),
      target_url: toStringValue(record.target_url),
      template: toStringValue(record.template) || 'homework',
      card_type: toStringValue(record.card_type),
    })
  }

  return { cards, indexHtmlCdnUrl }
}

const MAX_RESOURCE_CONTEXT_CANDIDATES = 3

async function buildCardFilesContext(): Promise<string> {
  const entries = resolveCardPaths()
  if (!entries.length) return ''

  const cacheKey = entries.map((e) => `${e.businessType}:${e.cardPath}`).join(',')
  const cached = cardFilesCache.get(cacheKey)
  if (cached && Date.now() - cached.ts < CARD_FILES_CACHE_TTL_MS) {
    return cached.context
  }

  const candidates: string[] = []
  for (const entry of entries) {
    if (candidates.length >= MAX_RESOURCE_CONTEXT_CANDIDATES) break
    const response = await fetchCardFilesFromChatServer(entry.cardPath)
    if (!response) continue

    const { cards, indexHtmlCdnUrl } = extractCardMetadata(response)
    if (!cards.length) continue

    for (const card of cards) {
      if (candidates.length >= MAX_RESOURCE_CONTEXT_CANDIDATES) break
      const resourceUrl = indexHtmlCdnUrl || card.target_url
      if (!resourceUrl) continue
      candidates.push([
        `- title: ${card.title}`,
        `  summary: ${card.summary}`,
        `  template: ${card.template}`,
        `  resourceUrl: ${resourceUrl}`,
        `  cardPath: ${entry.cardPath}`,
      ].join('\n'))
    }
  }

  if (!candidates.length) return ''

  const header = [
    'Available assignment resources (top candidates only):',
    'Use these only when the user asks to query or publish an existing homework/assignment resource.',
    'Do not use these for card generation. Card generation must use the user-provided content or structured card data.',
    '',
  ].join('\n')

  const context = `${header}${candidates.join('\n')}`
  cardFilesCache.set(cacheKey, { ts: Date.now(), context })
  return context
}

function rawUserRequestText(body: Record<string, unknown>): string {
  return toStringValue(body.rawMessage) || toStringValue(body.message)
}

function sanitizeResourcePathSegment(value: unknown): string {
  const normalized = toStringValue(value)
    .replace(/^#+/, '')
    .replace(/[^\w.-]+/g, '-')
    .replace(/-+/g, '-')
    .replace(/^-|-$/g, '')
  return normalized || 'unknown'
}

function resourceNamingRecord(body: Record<string, unknown>): Record<string, unknown> | null {
  const value = body.resourceNaming
  return value && typeof value === 'object' && !Array.isArray(value)
    ? value as Record<string, unknown>
    : null
}

export function buildOpenClawResourceNamingContext(body: Record<string, unknown>): string {
  if (resourceNamingRecord(body)) return ''

  const configured = resourceNamingRecord(body)
  const rawTaskNo = toStringValue(configured?.taskNo || body.taskNo)
  if (!rawTaskNo) return ''

  const sourceAgent = sanitizeResourcePathSegment(configured?.sourceAgent || body.providerId || 'openclaw')
  const taskNo = sanitizeResourcePathSegment(rawTaskNo)
  const pathMarker = toStringValue(configured?.pathMarker) || `ai/${sourceAgent}/${taskNo}`

  return [
    'Resource upload hint:',
    `Use objectKeyPrefix "${pathMarker}" for generated bucket resources when the upload tool supports it.`,
  ].join('\n')
}

function isExplicitAiCardRequestText(text: string): boolean {
  return (
    /卡片|card|连环画|漫画|comic|预览/i.test(text) &&
    /生成|创建|制作|渲染|做一个|来一个|帮我|发一个|发给|send|create|generate|make|render/i.test(text)
  )
}

export function shouldInjectCronContext(body: Record<string, unknown>): boolean {
  const rawMessage = rawUserRequestText(body)
  if (isExplicitAiCardRequestText(rawMessage)) return false
  return /(提醒|叫我|通知|告诉|闹钟|定时|timer|remind|分钟后|小时后|秒后|天之后)/iu.test(rawMessage)
}

export function shouldInjectCardFilesContext(body: Record<string, unknown>): boolean {
  if (toStringValue(process.env.OPENCLAW_ENABLE_CARD_FILES_CONTEXT).toLowerCase() !== 'true') return false
  const rawMessage = rawUserRequestText(body)
  if (isExplicitAiCardRequestText(rawMessage)) return false
  return /(作业|homework|assignment|assignments)/iu.test(rawMessage)
}

async function buildChatMessageForOpenClaw(body: Record<string, unknown>): Promise<string> {
  const message = toStringValue(body.message)
  const cronContext = shouldInjectCronContext(body) ? buildAgentChatCronContext(body) : ''
  const cardContext = shouldInjectCardFilesContext(body) ? await buildCardFilesContext() : ''
  const resourceNamingContext = buildOpenClawResourceNamingContext(body)
  const parts = [message]
  if (cronContext) parts.push(cronContext)
  if (cardContext) parts.push(cardContext)
  if (resourceNamingContext) parts.push(resourceNamingContext)
  return parts.join('\n')
}

// ── HTTP 工具 ──

function readRequestBody(req: IncomingMessage): Promise<Record<string, unknown>> {
  return new Promise((resolve, reject) => {
    let raw = ''
    req.setEncoding('utf8')
    req.on('data', (chunk: string) => {
      raw += chunk
      if (raw.length > 1024 * 1024) {
        reject(new Error('OpenClaw bridge request body is too large.'))
        req.destroy()
      }
    })
    req.on('end', () => {
      try {
        resolve(raw ? JSON.parse(raw) : {})
      } catch {
        reject(new Error('OpenClaw bridge request body is not valid JSON.'))
      }
    })
    req.on('error', reject)
  })
}

// ── SSE 事件流 ──

function writeSSE(res: ServerResponse, data: Record<string, unknown>): void {
  try {
    res.write(`data: ${JSON.stringify(data)}\n\n`)
  } catch {
    // 客户端断开——忽略写入错误
  }
}

export function streamChatEvents(
  client: OpenClawBridgeClient,
  runId: string,
  timeoutMs: number,
  res: ServerResponse,
  abortSignal?: AbortSignal,
): Promise<void> {
  return new Promise((resolve, reject) => {
    const startedAt = Date.now()
    let settled = false
    let unsubscribe: () => void = () => undefined
    const nextDeltaText = createOpenClawDeltaTextTracker()
    let lastMessageText = ''
    const toolResultTexts: string[] = []
    const resolveFinalText = (rawText: string): string => resolveOpenClawTerminalVisibleText({
      runId,
      startedAt,
      rawText: rawText || 'OpenClaw finished with no text.',
      fallback: '已处理完成，请查看当前结果。',
    })
    const hasRecoverableResult = (visibleText: string): boolean => (
      toolResultTexts.length > 0 || /https?:\/\//im.test(visibleText)
    )

    const cleanup = () => {
      clearTimeout(timer)
      unsubscribe()
      abortSignal?.removeEventListener('abort', abort)
    }

    const abort = () => {
      if (settled) return
      settled = true
      cleanup()
      resolve()
    }

    const timer = setTimeout(() => {
      if (settled) return
      const combined = toolResultTexts.length
        ? toolResultTexts.join('\n')
        : lastMessageText
      const visibleText = resolveFinalText(combined)
      settled = true
      cleanup()
      if (hasRecoverableResult(visibleText)) {
        writeSSE(res, { type: 'final', text: visibleText, state: 'timeout' })
        resolve()
        return
      }
      writeSSE(res, { type: 'error', message: `Stream timed out waiting for runId ${runId}.` })
      reject(new Error(`No terminal event received for runId ${runId}.`))
    }, timeoutMs)

    const finish = () => {
      if (settled) return
      settled = true
      cleanup()
      resolve()
    }

    abortSignal?.addEventListener('abort', abort, { once: true })

    unsubscribe = client.addEventListener((frame) => {
      if (frame.event !== 'chat') return
      const payload = (frame.payload ?? {}) as Record<string, unknown>
      if (toStringValue(payload.runId) !== runId) return

      const state = toStringValue(payload.state)
      const parsed = parseAssistantText(payload.message)
      if (parsed) lastMessageText = parsed

      for (const tool of extractToolCalls(payload)) {
        writeSSE(res, { type: 'tool_use', id: tool.id, name: tool.name, input: tool.input })
      }

      for (const result of extractToolResults(payload)) {
        writeSSE(res, { type: 'tool_result', toolUseId: result.toolUseId, content: result.content })
        if (result.content) toolResultTexts.push(result.content)
      }

      if (state === 'final') {
        const combined = toolResultTexts.length
          ? toolResultTexts.join('\n')
          : lastMessageText
        writeSSE(res, {
          type: 'final',
          text: resolveFinalText(combined),
          state,
        })
        finish()
        return
      }

      if (state === 'aborted') {
        writeSSE(res, { type: 'error', message: 'OpenClaw chat runtime aborted.' })
        finish()
        return
      }

      if (state === 'error') {
        writeSSE(res, {
          type: 'error',
          message: toStringValue(payload.errorMessage) || 'OpenClaw chat runtime failed.',
        })
        finish()
        return
      }

      const delta = nextDeltaText(payload)
      const visibleDelta = sanitizeOpenClawDeltaText(delta)
      if (delta && !visibleDelta) {
        console.log(`[stream-delta] SUPPRESSED deltaLen=${delta.length} delta="${delta.slice(0, 100).replace(/\n/g, '\\n')}"`)
      }
      if (visibleDelta) {
        writeSSE(res, { type: 'delta', text: visibleDelta })
      }
    })
  })
}

// ── 核心请求处理 ──

async function requestOpenClawAgent(body: Record<string, unknown>): Promise<{ text: string; runId: string; toolCalls: OpenClawBridgeToolCall[] }> {
  const message = await buildChatMessageForOpenClaw(body)
  if (!message) throw new Error('OpenClaw bridge request message is empty.')
  const pollTimeoutMs = resolveTimeout(body.pollTimeoutMs, DEFAULT_POLL_TIMEOUT_MS)
  const requestTimeoutMs = resolveTimeout(body.timeoutMs, DEFAULT_TIMEOUT_MS)
  const client = await bridgeClientManager.getClient({
    url: resolveGatewayUrl(),
    timeoutMs: requestTimeoutMs,
    pollTimeoutMs,
  })

  try {
    const idempotencyKey = createRequestId('agent-chat-android')
    const sendPayload = await client.request('chat.send', {
      sessionKey: toStringValue(body.sessionKey) || 'main',
      message,
      deliver: false,
      idempotencyKey,
    }, requestTimeoutMs) as { runId?: unknown } | undefined
    const runId = toStringValue(sendPayload?.runId) || idempotencyKey
    const result = await waitForTerminalChatEvent(client, runId, pollTimeoutMs)
    return { ...result, runId }
  } catch (error) {
    if (!client.isConnected()) bridgeClientManager.invalidate(client)
    throw error
  }
}

async function scheduleAgentChatReminderCron(body: Record<string, unknown>, intent: AgentChatReminderIntent): Promise<{ text: string; runId: string; toolCalls: OpenClawBridgeToolCall[] } | null> {
  const params = buildAgentChatReminderCronParams(body, intent)
  if (!params) return null

  const requestTimeoutMs = resolveTimeout(body.timeoutMs, DEFAULT_TIMEOUT_MS)
  const client = await bridgeClientManager.getClient({
    url: resolveGatewayUrl(),
    timeoutMs: requestTimeoutMs,
    pollTimeoutMs: resolveTimeout(body.pollTimeoutMs, DEFAULT_POLL_TIMEOUT_MS),
  })

  try {
    const response = await client.request('cron.add', params, requestTimeoutMs) as { id?: unknown } | undefined
    const runId = toStringValue(response?.id) || toStringValue(params.name)
    const schedule = params.schedule as { at?: unknown }
    const channel = currentConversationChannel(body)
    console.log(`[openclaw-agent-bridge] cron.add reminder job=${runId} channel=${channel} at=${toStringValue(schedule.at)}`)
    const deliveryText = isPersonalWorkspaceChannel(channel) ? '我会提醒你' : '我会在这个群里提醒你'
    const text = prefixRequesterMention([
      '已设置提醒！',
      '',
      `${intent.delayLabel}，${deliveryText}${intent.reminderText}`,
    ].join('\n'), body)
    return { runId, toolCalls: [], text }
  } catch (error) {
    if (!client.isConnected()) bridgeClientManager.invalidate(client)
    throw error
  }
}

function createConversationPostId(): string {
  return `post_${crypto.randomUUID()}`
}

function currentUserCanCreateGroupTask(body: Record<string, unknown>): boolean {
  const role = toStringValue(body.groupRole).toLowerCase()
  if (!role || role === 'owner') return true
  const ownerUserId = toStringValue(body.groupOwnerUserId || body.ownerUserId)
  if (!ownerUserId) return false
  const userIds = [
    toStringValue(body.userId),
    toStringValue(body.userPhone),
    toStringValue(body.creatorUserId),
    toStringValue(body.creatorPhone),
  ].filter(Boolean)
  return userIds.includes(ownerUserId)
}

function buildGroupTaskPermissionDeniedBridgeResult(
  body: Record<string, unknown>,
): { text: string; runId: string; toolCalls: OpenClawBridgeToolCall[]; attachments?: unknown[] } {
  return {
    runId: createRequestId('agentchat-group-task-denied'),
    toolCalls: [],
    text: prefixRequesterMention('这个属于群内容发布，需要群主操作。内容已生成草稿，可请群主确认发布。', body),
  }
}

async function createGroupTaskOnServer(
  body: Record<string, unknown>,
  intent: AgentChatGroupTaskIntent,
  postId: string,
): Promise<{ postId: string }> {
  const conversationId = currentConversationChannel(body)
  if (!conversationId || isPersonalWorkspaceChannel(conversationId)) {
    throw new Error('group task requires a group conversation')
  }

  const creatorUserId = toStringValue(body.userId) || 'openclaw'
  const creatorName = toStringValue(body.userName || body.createdByName) || creatorUserId || '炎图AI助手'
  const title = toStringValue(intent.title).slice(0, 80) || '群任务提醒'
  const summary = `定时提醒：${intent.delayLabel}。${intent.reminderText}`

  const response = await sendChatServerRequest({
    cmd: 'CONVERSATION_POST_CREATE',
    id: postId,
    channel: conversationId,
    conversationId,
    title,
    summary,
    taskKind: 'task',
    actionType: 'create',
    resourceType: 'task',
    resourceUrl: '',
    deadlineAt: String(intent.triggerAtMs),
    visibility: 'detail_only',
    createdById: creatorUserId,
    createdByName: creatorName,
  })
  if (toStringValue(response.event).toUpperCase() === 'ERROR' || toStringValue(response.status).toLowerCase() === 'error') {
    throw new Error(toStringValue(response.message || response.error || response.code) || 'CONVERSATION_POST_CREATE failed')
  }
  const event = toStringValue(response.event).toUpperCase()
  const responsePostId = toStringValue(response.postId || response.id)
  if (!responsePostId && event && event !== 'CONVERSATION_POST_CREATED') {
    throw new Error(`CONVERSATION_POST_CREATE unsupported by chat-server: ${event}`)
  }
  return { postId: responsePostId || postId }
}

function queueGroupTaskBackendSync(body: Record<string, unknown>, intent: AgentChatGroupTaskIntent, postId: string): void {
  setTimeout(() => {
    void createGroupTaskOnServer(body, intent, postId)
      .then((created) => {
        console.log(`[openclaw-agent-bridge] group task backend synced postId=${created.postId}`)
      })
      .catch((error) => {
        const message = error instanceof Error ? error.message : String(error)
        console.warn(`[openclaw-agent-bridge] group task backend sync failed postId=${postId}: ${message}`)
      })
  }, 0)
}

function buildGroupTaskUnderstandingPrompt(rawUserText: string, nowMs: number): string {
  const now = new Date(nowMs)
  return [
    'You are AgentChat group scheduled task parser.',
    'Understand the user request and return ONLY one JSON object. No markdown, no prose, no tool calls, no PowerShell.',
    'Do not create cron jobs yourself. The bridge will create the cron job from your JSON.',
    '',
    `Current time ISO: ${now.toISOString()}`,
    `Current local time: ${now.toLocaleString('zh-CN', { timeZone: 'Asia/Shanghai', hour12: false })}`,
    '',
    'Return schema:',
    '{"isGroupTask":true,"title":"short task title","reminderText":"message to remind the group","triggerAtMs":1770000000000,"delayLabel":"human-readable scheduled time"}',
    '',
    `User message: ${rawUserText}`,
  ].join('\n')
}

function buildStaleGroupTaskBridgeResult(
  body: Record<string, unknown>,
): { text: string; runId: string; toolCalls: OpenClawBridgeToolCall[]; attachments?: unknown[] } {
  return {
    runId: createRequestId('agentchat-group-task-stale'),
    toolCalls: [],
    text: prefixRequesterMention('这个群提醒时间已经过了，请重新指定一个未来时间。', body),
    attachments: [],
  }
}

function parseJsonRecordFromText(text: string): Record<string, unknown> | null {
  const raw = text.trim()
  if (!raw) return null
  const fenced = /```(?:json)?\s*([\s\S]*?)```/i.exec(raw)
  const candidate = fenced?.[1]?.trim() || raw
  try {
    const parsed = JSON.parse(candidate)
    return parsed && typeof parsed === 'object' && !Array.isArray(parsed) ? parsed as Record<string, unknown> : null
  } catch {
    const first = candidate.indexOf('{')
    const last = candidate.lastIndexOf('}')
    if (first < 0 || last <= first) return null
    try {
      const parsed = JSON.parse(candidate.slice(first, last + 1))
      return parsed && typeof parsed === 'object' && !Array.isArray(parsed) ? parsed as Record<string, unknown> : null
    } catch {
      return null
    }
  }
}

function timestampFromOpenClawValue(value: unknown): number | undefined {
  if (typeof value === 'number' && Number.isFinite(value)) return value
  const text = toStringValue(value)
  if (!text) return undefined
  if (/^\d+$/.test(text)) return Number(text)
  const parsed = Date.parse(text)
  return Number.isFinite(parsed) ? parsed : undefined
}

function normalizeOpenClawGroupTaskIntent(
  text: string,
  fallback: AgentChatGroupTaskIntent | null,
  nowMs: number,
): AgentChatGroupTaskIntent | null {
  const parsed = parseJsonRecordFromText(text)
  if (!parsed || parsed.isGroupTask === false) return null
  const triggerAtMs = timestampFromOpenClawValue(parsed.triggerAtMs || parsed.triggerAt || parsed.dueAt)
  if (!triggerAtMs) return null
  const delayMs = triggerAtMs - nowMs
  if (!Number.isFinite(delayMs) || delayMs <= 0 || delayMs > MAX_AGENTCHAT_REMINDER_DELAY_MS) return null
  const reminderText = toStringValue(parsed.reminderText || parsed.message || parsed.content) || fallback?.reminderText || ''
  const title = toStringValue(parsed.title || parsed.name) || reminderText || fallback?.title || ''
  if (!reminderText || !title) return null
  return {
    delayMs,
    delayLabel: toStringValue(parsed.delayLabel || parsed.timeLabel) || fallback?.delayLabel || new Date(triggerAtMs).toLocaleString('zh-CN', { timeZone: 'Asia/Shanghai', hour12: false }),
    reminderText,
    title,
    triggerAtMs,
  }
}

async function requestOpenClawGroupTaskIntent(
  body: Record<string, unknown>,
  rawUserText: string,
  fallback: AgentChatGroupTaskIntent | null,
): Promise<AgentChatGroupTaskIntent | null> {
  const nowMs = Date.now()
  const requestTimeoutMs = resolveTimeout(body.timeoutMs, DEFAULT_TIMEOUT_MS)
  const client = await bridgeClientManager.getClient({
    url: resolveGatewayUrl(),
    timeoutMs: requestTimeoutMs,
    pollTimeoutMs: resolveTimeout(body.pollTimeoutMs, DEFAULT_POLL_TIMEOUT_MS),
  })

  try {
    const idempotencyKey = createRequestId('agentchat-group-task-understand')
    const sendPayload = await client.request('chat.send', {
      sessionKey: `${toStringValue(body.sessionKey) || 'main'}:group-task-understand`,
      message: buildGroupTaskUnderstandingPrompt(rawUserText, nowMs),
      deliver: false,
      idempotencyKey,
    }, requestTimeoutMs) as { runId?: unknown } | undefined
    const runId = toStringValue(sendPayload?.runId) || idempotencyKey
    const result = await waitForTerminalChatEvent(client, runId, resolveTimeout(body.pollTimeoutMs, DEFAULT_POLL_TIMEOUT_MS))
    const intent = normalizeOpenClawGroupTaskIntent(result.text, fallback, nowMs)
    if (intent) return intent
    console.warn(`[openclaw-agent-bridge] group task understanding parse failed${fallback ? ', using local fallback' : ''}: ${result.text.slice(0, 160)}`)
    return fallback
  } catch (error) {
    if (!client.isConnected()) bridgeClientManager.invalidate(client)
    throw error
  }
}

async function scheduleAgentChatGroupTaskCron(
  body: Record<string, unknown>,
  intent: AgentChatGroupTaskIntent,
  postId: string,
): Promise<{ text: string; runId: string; toolCalls: OpenClawBridgeToolCall[]; attachments?: unknown[] } | null> {
  const params = buildAgentChatGroupTaskCronParams(body, intent, postId, Date.now())
  if (!params) return null

  const requestTimeoutMs = resolveTimeout(body.timeoutMs, DEFAULT_TIMEOUT_MS)
  const client = await bridgeClientManager.getClient({
    url: resolveGatewayUrl(),
    timeoutMs: requestTimeoutMs,
    pollTimeoutMs: resolveTimeout(body.pollTimeoutMs, DEFAULT_POLL_TIMEOUT_MS),
  })

  try {
    const response = await client.request('cron.add', params, requestTimeoutMs) as { id?: unknown } | undefined
    const runId = toStringValue(response?.id) || toStringValue(params.name)
    const schedule = params.schedule as { at?: unknown }
    const channel = currentConversationChannel(body)
    console.log(`[openclaw-agent-bridge] cron.add group task job=${runId} channel=${channel} postId=${postId} at=${toStringValue(schedule.at)}`)
    const text = prefixRequesterMention([
      '群任务已创建。',
      '',
      `${intent.delayLabel}，我会在群里提醒大家：${intent.reminderText}`,
    ].join('\n'), body)
    return {
      runId,
      toolCalls: [],
      text,
      attachments: [buildGroupTaskNativePostAttachment(body, intent, postId)],
    }
  } catch (error) {
    if (!client.isConnected()) bridgeClientManager.invalidate(client)
    throw error
  }
}

async function createAndScheduleAgentChatGroupTask(
  body: Record<string, unknown>,
  rawUserText: string,
): Promise<{ text: string; runId: string; toolCalls: OpenClawBridgeToolCall[]; attachments?: unknown[] } | null> {
  if (!currentUserCanCreateGroupTask(body)) return buildGroupTaskPermissionDeniedBridgeResult(body)
  const fallbackIntent = tryParseAgentChatGroupTaskIntent(rawUserText)
  const understoodIntent = await requestOpenClawGroupTaskIntent(body, rawUserText, fallbackIntent)
  if (!understoodIntent) return null
  const postId = createConversationPostId()
  const scheduled = await scheduleAgentChatGroupTaskCron(body, understoodIntent, postId)
  if (!scheduled && understoodIntent.triggerAtMs <= Date.now() + 10_000) return buildStaleGroupTaskBridgeResult(body)
  if (scheduled) queueGroupTaskBackendSync(body, understoodIntent, postId)
  return scheduled
}

async function sendOpenClawChat(
  body: Record<string, unknown>,
): Promise<{ client: OpenClawBridgeClient; runId: string; pollTimeoutMs: number }> {
  const message = await buildChatMessageForOpenClaw(body)
  if (!message) throw new Error('OpenClaw bridge request message is empty.')
  const pollTimeoutMs = resolveTimeout(body.pollTimeoutMs, DEFAULT_POLL_TIMEOUT_MS)
  const requestTimeoutMs = resolveTimeout(body.timeoutMs, DEFAULT_TIMEOUT_MS)
  const client = await bridgeClientManager.getClient({
    url: resolveGatewayUrl(),
    timeoutMs: requestTimeoutMs,
    pollTimeoutMs,
  })

  try {
    const idempotencyKey = createRequestId('agent-chat-android')
    const sendPayload = await client.request('chat.send', {
      sessionKey: toStringValue(body.sessionKey) || 'main',
      message,
      deliver: false,
      idempotencyKey,
    }, requestTimeoutMs) as { runId?: unknown } | undefined
    const runId = toStringValue(sendPayload?.runId) || idempotencyKey
    return { client, runId, pollTimeoutMs }
  } catch (error) {
    if (!client.isConnected()) bridgeClientManager.invalidate(client)
    throw error
  }
}

async function handleStreamRequest(req: IncomingMessage, res: ServerResponse, body: Record<string, unknown>): Promise<void> {
  const message = toStringValue(body.message)
  if (!message) {
    writeJsonResponse(res, 400, { ok: false, error: 'OpenClaw bridge request message is empty.' })
    return
  }

  res.writeHead(200, {
    'Content-Type': 'text/event-stream; charset=utf-8',
    'Cache-Control': 'no-cache',
    Connection: 'keep-alive',
    'X-Accel-Buffering': 'no',
  })

  let client: OpenClawBridgeClient | undefined
  try {
    const sent = await sendOpenClawChat(body)
    client = sent.client
    console.log(`[openclaw-agent-bridge] stream started runId=${sent.runId.slice(0, 40)}`)

    const abortController = new AbortController()
    req.on('close', () => {
      console.log(`[openclaw-agent-bridge] stream client disconnected runId=${sent.runId.slice(0, 40)}`)
      abortController.abort()
    })

    await streamChatEvents(client, sent.runId, sent.pollTimeoutMs, res, abortController.signal)
    console.log(`[openclaw-agent-bridge] stream finished runId=${sent.runId.slice(0, 40)}`)
  } catch (error) {
    const errMsg = error instanceof Error ? error.message : String(error)
    console.log(`[openclaw-agent-bridge] stream failed: ${errMsg}`)
    writeSSE(res, { type: 'error', message: errMsg })
    if (client && !client.isConnected()) bridgeClientManager.invalidate(client)
  } finally {
    try { res.end() } catch { /* ignore */ }
  }
}

function writeJsonResponse(res: ServerResponse, statusCode: number, body: Record<string, unknown>): void {
  const payload = Buffer.from(JSON.stringify(body), 'utf8')
  res.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': payload.length,
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Headers': 'Content-Type',
    'Access-Control-Allow-Methods': 'POST, OPTIONS',
    Connection: 'close',
  })
  res.end(payload)
}

/**
 * Agent bridge 的主 entrypoint —— 处理 POST /api/openclaw-agent。
 */
export async function handleOpenClawAgentBridgeRequest(req: IncomingMessage, res: ServerResponse): Promise<void> {
  if (req.method === 'OPTIONS') {
    writeJsonResponse(res, 204, { ok: true })
    return
  }

  if (req.method !== 'POST') {
    writeJsonResponse(res, 405, { ok: false, error: 'Method not allowed.' })
    return
  }

  try {
    const body = await readRequestBody(req)
    const msgPreview = toStringValue(body.message).replace(/\s+/g, ' ').trim().slice(0, 120)
    console.log(`[openclaw-agent-bridge] -> taskNo=${toStringValue(body.taskNo)} providerId=${toStringValue(body.providerId)} workspaceId=${toStringValue(body.workspaceId)} sessionKey=${toStringValue(body.sessionKey) || 'main'} stream=${!!body.stream} message="${msgPreview}" conversationId="${toStringValue(body.conversationId)}" groupId="${toStringValue(body.groupId)}"`)

    const rawUserText = toStringValue(body.rawMessage) || toStringValue(body.message)
    if (isExplicitGroupReminderText(rawUserText)) {
      const scheduled = await createAndScheduleAgentChatGroupTask(body, rawUserText)
      if (scheduled) {
        const textPreview = scheduled.text.replace(/\s+/g, ' ').trim().slice(0, 100)
        console.log(`[openclaw-agent-bridge] <- ok group-task text="${textPreview}" toolCalls=${scheduled.toolCalls.length}`)
        if (body.stream) {
          res.writeHead(200, {
            'Content-Type': 'text/event-stream; charset=utf-8',
            'Cache-Control': 'no-cache',
            Connection: 'keep-alive',
            'X-Accel-Buffering': 'no',
          })
          writeSSE(res, { type: 'delta', text: scheduled.text })
          writeSSE(res, { type: 'final', text: scheduled.text, state: 'final' })
          res.end()
          return
        }
        writeJsonResponse(res, 200, { ok: true, source: 'openclaw', ...scheduled })
        return
      }
    }

    const reminderIntent = tryParseAgentChatReminderIntent(rawUserText)
    if (reminderIntent) {
      const scheduled = await scheduleAgentChatReminderCron(body, reminderIntent)
      if (scheduled) {
        const textPreview = scheduled.text.replace(/\s+/g, ' ').trim().slice(0, 100)
        console.log(`[openclaw-agent-bridge] <- ok text="${textPreview}" toolCalls=${scheduled.toolCalls.length}`)
        if (body.stream) {
          res.writeHead(200, {
            'Content-Type': 'text/event-stream; charset=utf-8',
            'Cache-Control': 'no-cache',
            Connection: 'keep-alive',
            'X-Accel-Buffering': 'no',
          })
          writeSSE(res, { type: 'delta', text: scheduled.text })
          writeSSE(res, { type: 'final', text: scheduled.text, state: 'final' })
          res.end()
          return
        }
        writeJsonResponse(res, 200, { ok: true, source: 'openclaw', ...scheduled })
        return
      }
    }

    if (body.stream) {
      return await handleStreamRequest(req, res, body)
    }

    const result = await requestOpenClawAgent(body)
    const textPreview = result.text.replace(/\s+/g, ' ').trim().slice(0, 100)
    console.log(`[openclaw-agent-bridge] <- ok text="${textPreview}" toolCalls=${result.toolCalls.length}`)
    writeJsonResponse(res, 200, { ok: true, source: 'openclaw', ...result })
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    console.log(`[openclaw-agent-bridge] ERROR ${message}`)
    writeJsonResponse(res, 502, {
      ok: false,
      error: message,
      hint: '请确认宿主机 OpenClaw gateway 正在运行 (ws://127.0.0.1:18789)，且 agent-chat-android-bridge 已获 operator 权限。',
    })
  }
}
