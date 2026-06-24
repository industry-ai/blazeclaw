/**
 * OpenClaw Runner — AI 群成员持久客户端。
 *
 * 持久 TCP 连接到 chat-server，以 AI 用户身份 JOIN 房间，监听 PRIVMSG，
 * 检测 @炎图AI助手 → 调本地 OpenClaw Gateway → 结果以 PRIVMSG 发回聊天室。
 *
 * 启动：
 *   npx tsx server/openclaw-runner.ts
 *   OPENCLAW_RUNNER_CHAT_HOST=192.168.20.211 npx tsx server/openclaw-runner.ts
 *
 * 环境变量：
 *   OPENCLAW_RUNNER_CHAT_HOST      — chat-server TCP 地址 (默认 192.168.20.211)
 *   OPENCLAW_RUNNER_CHAT_PORT      — chat-server TCP 端口 (默认 8765)
 *   OPENCLAW_RUNNER_AI_IDENTITY    — AI 助手的 @名称 (默认 "炎图AI助手")
 *   OPENCLAW_RUNNER_AI_SESSION_ID  — AI 的 session_id (预获取，可选)
 *   OPENCLAW_RUNNER_ROOMS          — 逗号分隔的 JOIN 房间列表
 *                                     (默认 "#roadshow-room")
 *   OPENCLAW_RUNNER_GATEWAY_URL    — OpenClaw Gateway 地址
 *                                     (默认 ws://127.0.0.1:18789)
 *   OPENCLAW_GATEWAY_URL           — fallback gateway URL
 */
import crypto from 'node:crypto'
import net from 'node:net'
import {
  toStringValue,
  createRequestId,
  isPersonalWorkspaceChannel,
  normalizeAgentMentionText,
  sanitizeOpenClawVisibleText,
  isExplicitGroupReminderText,
  tryParseAgentChatReminderIntent,
  tryParseAgentChatGroupTaskIntent,
  buildAgentChatReminderCronParams,
  buildAgentChatGroupTaskCronParams,
  resolveGatewayUrl,
  resolveChatHost,
  resolveChatPort,
  buildOpenClawRunnerMessage,
  fallbackForLowInformationCardResult,
  parseAssistantText,
  bridgeClientManager,
  waitForTerminalChatEvent,
  buildHBPCHeader,
  parseHBPCHeader,
  readExactlyNBytes,
  readHBPCResponse,
  HBPC_IRC_MESSAGE_REQ,
  HBPC_IRC_MESSAGE_RESP,
  HBPC_PROTO_VERSION,
  HBPC_MAX_PAYLOAD_SIZE,
  HBPC_HEADER_SIZE,
  type AgentChatReminderIntent,
  type AgentChatGroupTaskIntent,
} from './openclaw-shared'

// ══════════════════════════════════════════════
// 配置解析
// ══════════════════════════════════════════════

const RUNNER_CHAT_HOST = toStringValue(
  process.env.OPENCLAW_RUNNER_CHAT_HOST || process.env.CHAT_TCP_HOST || process.env.VITE_CHAT_TCP_HOST,
) || resolveChatHost()
const RUNNER_CHAT_PORT = Number(process.env.OPENCLAW_RUNNER_CHAT_PORT || process.env.CHAT_TCP_PORT || process.env.VITE_CHAT_TCP_PORT) || resolveChatPort()
const AI_IDENTITY = toStringValue(process.env.OPENCLAW_RUNNER_AI_IDENTITY || '炎图AI助手')
const AI_SESSION_ID = process.env.OPENCLAW_RUNNER_AI_SESSION_ID || undefined
const RUNNER_ROOMS = toStringValue(process.env.OPENCLAW_RUNNER_ROOMS || '#roadshow-room,#personal-workspace')
  .split(',').map((s) => s.trim()).filter(Boolean)

const DEFAULT_TIMEOUT_MS = 10000
const DEFAULT_POLL_TIMEOUT_MS = 600000
const RECONNECT_BASE_MS = 1000
const RECONNECT_MAX_MS = 30000

// ══════════════════════════════════════════════
// 协作协议编码
// ══════════════════════════════════════════════

const COLLABORATION_PREFIX = 'c:agentchat.collaboration'

function isCollaborationMessage(message: string): boolean {
  return message.startsWith(COLLABORATION_PREFIX)
}


// ══════════════════════════════════════════════
// 消息结构
// ══════════════════════════════════════════════

interface RunnerMessage {
  channel: string
  message: string
  from: string
  messageId: string
  userId?: string
  userPhone?: string
  ts?: number
}

// ══════════════════════════════════════════════
// 序列号
// ══════════════════════════════════════════════

let runnerSeq = 0
function nextSeq(): number {
  runnerSeq = (runnerSeq + 1) & 0xffffffff
  return runnerSeq
}

// ══════════════════════════════════════════════
// Runner 核心
// ══════════════════════════════════════════════

class OpenClawRunner {
  private socket: net.Socket | null = null
  private sessionId: bigint = 0n
  private currentRooms: Set<string> = new Set()
  private processingMessages: Set<string> = new Set()
  private reconnectAttempt = 0
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null
  private running = false

  async start(): Promise<void> {
    this.running = true
    console.log(`[openclaw-runner] starting AI identity="${AI_IDENTITY}" host=${RUNNER_CHAT_HOST}:${RUNNER_CHAT_PORT} rooms=[${RUNNER_ROOMS.join(',')}]`)
    await this.connectAndJoin()
  }

  async stop(): Promise<void> {
    this.running = false
    this.clearReconnectTimer()
    this.socket?.destroy()
    this.socket = null
    console.log('[openclaw-runner] stopped')
  }

  // ── 连接与重连 ──

  private clearReconnectTimer(): void {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
  }

  private scheduleReconnect(): void {
    if (!this.running) return
    this.clearReconnectTimer()
    const delay = Math.min(RECONNECT_BASE_MS * Math.pow(2, this.reconnectAttempt), RECONNECT_MAX_MS)
    console.log(`[openclaw-runner] reconnecting in ${delay}ms (attempt ${this.reconnectAttempt + 1})`)
    this.reconnectTimer = setTimeout(() => {
      this.reconnectAttempt += 1
      void this.connectAndJoin()
    }, delay)
  }

  private async connectAndJoin(): Promise<void> {
    try {
      // 1. 连接 OpenClaw Gateway
      const gwUrl = process.env.OPENCLAW_RUNNER_GATEWAY_URL
        ? toStringValue(process.env.OPENCLAW_RUNNER_GATEWAY_URL)
        : resolveGatewayUrl()
      await bridgeClientManager.getClient({
        url: gwUrl,
        timeoutMs: DEFAULT_TIMEOUT_MS,
        pollTimeoutMs: DEFAULT_POLL_TIMEOUT_MS,
      })
      console.log(`[openclaw-runner] OpenClaw Gateway connected ${gwUrl}`)

      // 2. 连接 chat-server
      const socket = await this.connectChatServer()
      this.socket = socket

      // 3. 认证获取 session_id
      if (AI_SESSION_ID) {
        this.sessionId = BigInt(AI_SESSION_ID)
        console.log(`[openclaw-runner] using pre-configured session_id=${AI_SESSION_ID}`)
      } else {
        await this.authenticate()
      }

      // 4. JOIN 所有房间
      for (const room of RUNNER_ROOMS) {
        await this.joinRoom(room)
      }

      this.reconnectAttempt = 0
      console.log(`[openclaw-runner] ready — joined ${this.currentRooms.size} rooms`)

      // 5. 开始读取消息
      this.readLoop().catch((error) => {
        console.error(`[openclaw-runner] read loop error: ${error instanceof Error ? error.message : String(error)}`)
        this.socket?.destroy()
        this.scheduleReconnect()
      })
    } catch (error) {
      console.error(`[openclaw-runner] connect failed: ${error instanceof Error ? error.message : String(error)}`)
      this.scheduleReconnect()
    }
  }

  private connectChatServer(): Promise<net.Socket> {
    return new Promise((resolve, reject) => {
      const socket = net.connect({ host: RUNNER_CHAT_HOST, port: RUNNER_CHAT_PORT })
      const timer = setTimeout(() => {
        socket.destroy()
        reject(new Error(`chat-server connection timeout (${RUNNER_CHAT_HOST}:${RUNNER_CHAT_PORT})`))
      }, DEFAULT_TIMEOUT_MS)

      socket.once('connect', () => {
        clearTimeout(timer)
        console.log(`[openclaw-runner] TCP connected to ${RUNNER_CHAT_HOST}:${RUNNER_CHAT_PORT}`)
        resolve(socket)
      })
      socket.once('error', (error) => {
        clearTimeout(timer)
        reject(error)
      })
    })
  }

  private async authenticate(): Promise<void> {
    // 发送 session LOGIN 请求（type 9 session，类型 25 登录）
    const loginPayload = {
      cmd: 'LOGIN',
      identity: `ai_${AI_IDENTITY}`,
      type: 'ai_runner',
    }
    const raw = Buffer.from(JSON.stringify(loginPayload), 'utf8')
    const header = buildHBPCHeader(9, raw.length, 0n) // type=9 Session
    const socket = this.socket!
    await new Promise<void>((resolve, reject) => {
      socket.write(Buffer.concat([header, raw]), (err) => (err ? reject(err) : resolve()))
    })

    // 读取响应
    try {
      const response = await readHBPCResponse(socket)
      const sid = toStringValue(response.sessionId || response.session_id)
      if (sid) {
        this.sessionId = BigInt(sid)
        console.log(`[openclaw-runner] authenticated session_id=${sid}`)
      } else {
        console.log(`[openclaw-runner] auth response: ${JSON.stringify(response).slice(0, 120)}`)
        this.sessionId = 0n
      }
    } catch (error) {
      console.log(`[openclaw-runner] auth response read error: ${error instanceof Error ? error.message : String(error)}, continuing with session_id=0`)
      this.sessionId = 0n
    }
  }

  private async joinRoom(channel: string): Promise<void> {
    const joinPayload = { cmd: 'JOIN', channel }
    const raw = Buffer.from(JSON.stringify(joinPayload), 'utf8')
    const header = buildHBPCHeader(HBPC_IRC_MESSAGE_REQ, raw.length, this.sessionId)
    const socket = this.socket!
    await new Promise<void>((resolve, reject) => {
      socket.write(Buffer.concat([header, raw]), (err) => (err ? reject(err) : resolve()))
    })
    this.currentRooms.add(channel)
    console.log(`[openclaw-runner] JOIN ${channel}`)
  }

  // ── 消息读取循环 ──

  private async readLoop(): Promise<void> {
    const socket = this.socket!
    if (!socket) return

    while (this.running && socket.readable) {
      try {
        const headerBuffer = await readExactlyNBytes(socket, HBPC_HEADER_SIZE)
        const header = parseHBPCHeader(headerBuffer)
        if (!header || header.magic !== 'HBPC') {
          console.warn(`[openclaw-runner] invalid frame magic=${header?.magic ?? 'null'}`)
          break
        }
        if (header.version !== HBPC_PROTO_VERSION) {
          console.warn(`[openclaw-runner] unsupported version=${header.version}`)
          continue
        }
        if (header.payloadLen > HBPC_MAX_PAYLOAD_SIZE) {
          console.warn(`[openclaw-runner] payload too large ${header.payloadLen}`)
          continue
        }

        const payloadBuffer = header.payloadLen > 0
          ? await readExactlyNBytes(socket, header.payloadLen)
          : Buffer.alloc(0)

        if (header.type === HBPC_IRC_MESSAGE_RESP) {
          const payload = this.parsePayload(payloadBuffer)
          if (!payload) continue

          const event = toStringValue(payload.event).toUpperCase()
          if (event === 'PRIVMSG') {
            await this.handlePrivmsg({
              channel: toStringValue(payload.channel),
              message: toStringValue(payload.message),
              from: toStringValue(payload.from),
              messageId: toStringValue(payload.messageId || payload.message_id || payload.id) || createRequestId('msg'),
              userId: toStringValue(payload.userId || payload.user_id),
              userPhone: toStringValue(payload.userPhone || payload.user_phone || payload.phone),
              ts: Number(payload.ts || payload.timestamp || Date.now()),
            })
          } else if (event === 'JOIN_ACK' || event === 'CONNECTED') {
            console.log(`[openclaw-runner] event ${event} ${toStringValue(payload.channel)}`)
          }
        } else if (header.type === 9) {
          // Session 响应（PING/PONG 等），忽略
        }
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error)
        if (message.includes('socket closed') || message.includes('closed before')) {
          console.log('[openclaw-runner] socket closed, will reconnect')
        } else {
          console.error(`[openclaw-runner] read error: ${message}`)
        }
        break
      }
    }

    console.log('[openclaw-runner] read loop exited')
    this.scheduleReconnect()
  }

  private parsePayload(buffer: Buffer): Record<string, unknown> | null {
    const text = buffer.toString('utf8')
    if (!text) return null
    try {
      const parsed = JSON.parse(text)
      return parsed && typeof parsed === 'object' && !Array.isArray(parsed)
        ? parsed as Record<string, unknown>
        : null
    } catch {
      return null
    }
  }

  // ── 消息判断 ──

  private shouldRespond(msg: RunnerMessage): boolean {
    // 1. 忽略自己的消息
    if (msg.from === AI_IDENTITY || msg.from === `ai_${AI_IDENTITY}`) return false

    // 2. 忽略协作协议消息
    if (isCollaborationMessage(msg.message)) return false

    // 3. 忽略 AGENT_TYPING 标记
    if (msg.message?.startsWith('[::AGENT_TYPING::]')) return false

    // 4. 忽略空消息
    if (!msg.message || !msg.message.trim()) return false

    // 5. 个人工作空间——所有消息都响应
    if (isPersonalWorkspaceChannel(msg.channel)) return true

    // 6. 群聊——只响应 @炎图AI助手
    return this.isMentioned(msg.message)
  }

  private isMentioned(text: string): boolean {
    return text.includes(`@${AI_IDENTITY}`) || text.includes(`@${AI_IDENTITY.replace(/​/g, '')}`) // 含零宽空格
  }

  // ── 消息处理 ──

  private async handlePrivmsg(msg: RunnerMessage): Promise<void> {
    if (!this.shouldRespond(msg)) return

    // 去重
    if (this.processingMessages.has(msg.messageId)) return
    if (this.processingMessages.size > 500) this.processingMessages.clear()
    this.processingMessages.add(msg.messageId)

    const userText = this.extractUserText(msg.message)
    console.log(`[openclaw-runner] processing channel=${msg.channel} from=${msg.from || msg.userPhone || '-'} text="${userText.slice(0, 80)}"`)

    // 发送打字指示
    await this.sendTypingIndicator(msg.channel, true)

    try {
      // 构建请求体（模拟 bridge body）
      const body = this.buildRequestContext(msg, userText)

      // 检查群任务提醒
      if (isExplicitGroupReminderText(userText)) {
        const result = await this.handleGroupTask(body, userText)
        if (result) {
          await this.sendTypingIndicator(msg.channel, false)
          await this.sendReply(msg.channel, result.text, msg.messageId)
          return
        }
      }

      // 检查个人提醒
      const reminderIntent = tryParseAgentChatReminderIntent(userText)
      if (reminderIntent) {
        const result = await this.handlePersonalReminder(body, reminderIntent)
        if (result) {
          await this.sendTypingIndicator(msg.channel, false)
          await this.sendReply(msg.channel, result.text, msg.messageId)
          return
        }
      }

      // 通用 AI 处理
      const aiText = await this.processWithOpenClaw(body)
      await this.sendTypingIndicator(msg.channel, false)

      // 个人工作空间不加 @ 前缀
      const replyText = isPersonalWorkspaceChannel(msg.channel)
        ? aiText
        : this.prefixMention(aiText, msg)

      await this.sendReply(msg.channel, replyText, msg.messageId)
    } catch (error) {
      await this.sendTypingIndicator(msg.channel, false)
      const errMsg = error instanceof Error ? error.message : String(error)
      console.error(`[openclaw-runner] process error: ${errMsg}`)
      await this.sendReply(msg.channel, '抱歉，助手暂时无法处理您的请求，请稍后再试。', msg.messageId)
    }
  }

  private extractUserText(text: string): string {
    return normalizeAgentMentionText(text)
  }

  private buildRequestContext(msg: RunnerMessage, userText: string): Record<string, unknown> {
    return {
      message: userText,
      rawMessage: msg.message,
      conversationId: msg.channel,
      groupId: msg.channel.startsWith('#') ? msg.channel : undefined,
      messageId: msg.messageId,
      userId: msg.userId || msg.userPhone || msg.from,
      userPhone: msg.userPhone || msg.from,
    }
  }

  private prefixMention(text: string, msg: RunnerMessage): string {
    const content = toStringValue(text)
    if (!content) return content
    const mentionId = msg.userPhone || msg.userId || msg.from
    if (!mentionId) return content
    const mention = `@${mentionId}`
    if (content.startsWith(mention) || /^@\S+\s/u.test(content)) return content
    return `${mention} ${content}`
  }

  // ── AI 处理 ──

  private async processWithOpenClaw(body: Record<string, unknown>): Promise<string> {
    const client = await bridgeClientManager.getClient({
      url: process.env.OPENCLAW_RUNNER_GATEWAY_URL
        ? toStringValue(process.env.OPENCLAW_RUNNER_GATEWAY_URL)
        : resolveGatewayUrl(),
      timeoutMs: DEFAULT_TIMEOUT_MS,
      pollTimeoutMs: DEFAULT_POLL_TIMEOUT_MS,
    })

    const idempotencyKey = createRequestId('openclaw-runner')
    const requestMessage = buildOpenClawRunnerMessage(body)
    const sendPayload = await client.request('chat.send', {
      sessionKey: 'openclaw-runner:main',
      message: requestMessage,
      deliver: false,
      idempotencyKey,
    }, DEFAULT_TIMEOUT_MS) as { runId?: unknown } | undefined

    const runId = toStringValue(sendPayload?.runId) || idempotencyKey
    console.log(`[openclaw-runner] AI runId=${runId.slice(0, 40)}`)
    const result = await waitForTerminalChatEvent(client, runId, DEFAULT_POLL_TIMEOUT_MS)
    console.log(`[openclaw-runner] AI result text="${result.text.replace(/\s+/g, ' ').slice(0, 100)}" toolCalls=${result.toolCalls.length}`)
    return fallbackForLowInformationCardResult(result.text || '已处理完成。', toStringValue(body.message))
  }

  private async handlePersonalReminder(
    body: Record<string, unknown>,
    intent: AgentChatReminderIntent,
  ): Promise<{ text: string } | null> {
    const params = buildAgentChatReminderCronParams(body, intent)
    if (!params) return null

    const client = await bridgeClientManager.getClient({
      url: process.env.OPENCLAW_RUNNER_GATEWAY_URL
        ? toStringValue(process.env.OPENCLAW_RUNNER_GATEWAY_URL)
        : resolveGatewayUrl(),
      timeoutMs: DEFAULT_TIMEOUT_MS,
      pollTimeoutMs: DEFAULT_POLL_TIMEOUT_MS,
    })

    try {
      const response = await client.request('cron.add', params, DEFAULT_TIMEOUT_MS) as { id?: unknown } | undefined
      const runId = toStringValue(response?.id) || toStringValue(params.name)
      const schedule = params.schedule as { at?: unknown }
      const channel = toStringValue(body.conversationId)
      console.log(`[openclaw-runner] cron.add reminder job=${runId} channel=${channel} at=${toStringValue(schedule.at)}`)
      const isPersonal = isPersonalWorkspaceChannel(channel)
      const deliveryText = isPersonal ? '我会提醒你' : '我会在这个群里提醒你'
      const text = [
        '已设置提醒！',
        '',
        `${intent.delayLabel}，${deliveryText}${intent.reminderText}`,
      ].join('\n')
      return { text }
    } catch (error) {
      console.error(`[openclaw-runner] cron.add error: ${error instanceof Error ? error.message : String(error)}`)
      return null
    }
  }

  private async handleGroupTask(
    body: Record<string, unknown>,
    userText: string,
  ): Promise<{ text: string } | null> {
    const intent = tryParseAgentChatGroupTaskIntent(userText)
    if (!intent) return null

    const postId = `post_${crypto.randomUUID()}`
    const params = buildAgentChatGroupTaskCronParams(body, intent, postId, Date.now())
    if (!params) return null

    const client = await bridgeClientManager.getClient({
      url: process.env.OPENCLAW_RUNNER_GATEWAY_URL
        ? toStringValue(process.env.OPENCLAW_RUNNER_GATEWAY_URL)
        : resolveGatewayUrl(),
      timeoutMs: DEFAULT_TIMEOUT_MS,
      pollTimeoutMs: DEFAULT_POLL_TIMEOUT_MS,
    })

    try {
      const response = await client.request('cron.add', params, DEFAULT_TIMEOUT_MS) as { id?: unknown } | undefined
      const runId = toStringValue(response?.id) || toStringValue(params.name)
      const schedule = params.schedule as { at?: unknown }
      const channel = toStringValue(body.conversationId)
      console.log(`[openclaw-runner] cron.add group task job=${runId} channel=${channel} postId=${postId} at=${toStringValue(schedule.at)}`)
      const text = [
        '群任务已创建。',
        '',
        `${intent.delayLabel}，我会在群里提醒大家：${intent.reminderText}`,
      ].join('\n')
      return { text }
    } catch (error) {
      console.error(`[openclaw-runner] cron.add group task error: ${error instanceof Error ? error.message : String(error)}`)
      return null
    }
  }

  // ── 回复投递 ──

  private async sendReply(channel: string, text: string, replyToMessageId: string): Promise<void> {
    await this.sendChatFrame({
      cmd: 'PRIVMSG',
      channel,
      message: text,
      replyTo: replyToMessageId,
    })
    console.log(`[openclaw-runner] reply sent channel=${channel} text="${text.slice(0, 80)}"`)
  }

  private async sendTypingIndicator(channel: string, isTyping: boolean): Promise<void> {
    try {
      const payload: Record<string, unknown> = {
        cmd: 'AGENT_BROADCAST',
        channel,
        typing: isTyping,
        agentRequestId: 'openclaw-runner',
      }
      const raw = Buffer.from(JSON.stringify(payload), 'utf8')
      const header = buildHBPCHeader(HBPC_IRC_MESSAGE_REQ, raw.length, this.sessionId || 0n)
      const socket = this.socket
      if (socket?.readable) {
        await new Promise<void>((resolve) => {
          socket.write(Buffer.concat([header, raw]), () => resolve())
        })
      }
    } catch {
      // typing 指示失败不影响主要功能
    }
  }

  private async sendChatFrame(payload: Record<string, unknown>): Promise<void> {
    const raw = Buffer.from(JSON.stringify(payload), 'utf8')
    if (raw.length > HBPC_MAX_PAYLOAD_SIZE) throw new Error('payload too large')
    const header = buildHBPCHeader(HBPC_IRC_MESSAGE_REQ, raw.length, this.sessionId || 0n)
    const socket = this.socket
    if (!socket) throw new Error('runner not connected')
    await new Promise<void>((resolve, reject) => {
      socket.write(Buffer.concat([header, raw]), (err) => (err ? reject(err) : resolve()))
    })
  }
}

// ══════════════════════════════════════════════
// 入口
// ══════════════════════════════════════════════

async function main(): Promise<void> {
  const runner = new OpenClawRunner()

  const shutdown = async () => {
    console.log('[openclaw-runner] shutting down...')
    await runner.stop()
    process.exit(0)
  }

  process.on('SIGINT', shutdown)
  process.on('SIGTERM', shutdown)
  process.on('uncaughtException', (err) => {
    console.error(`[openclaw-runner] uncaught exception: ${err.stack || err.message}`)
    process.exit(1)
  })
  process.on('unhandledRejection', (reason) => {
    console.error(`[openclaw-runner] unhandled rejection: ${reason instanceof Error ? reason.stack || reason.message : String(reason)}`)
  })

  await runner.start()
}

void main()
