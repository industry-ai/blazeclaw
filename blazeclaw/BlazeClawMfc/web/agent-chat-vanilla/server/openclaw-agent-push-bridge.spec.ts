import net from 'node:net'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import {
  clearAgentPushIdempotencyCache,
  ensureOpenClawAgentPushEnv,
  rememberAgentPushIdempotencyKey,
  resolveAgentPushRequestBody,
  sendAgentPushToChatServer,
  sendChatServerRequest,
  validateAgentPushBody,
  type AgentPushPayload,
} from './openclaw-agent-push-bridge'
import { resolveOpenClawAgentPushToken } from './openclaw-shared'

const HEADER_SIZE = 64
const PROTO_MAGIC = Buffer.from('HBPC', 'ascii')

function clearPushBridgeEnv(): void {
  delete process.env.OPENCLAW_AGENT_PUSH_TOKEN
  delete process.env.OPENCLAW_AGENT_PUSH_PUBLIC_URL
  delete process.env.AGENTCHAT_OPENCLAW_AGENT_PUSH_PUBLIC_URL
  delete process.env.AGENTCHAT_OPENCLAW_AGENT_PUSH_URL
  delete process.env.AGENTCHAT_OPENCLAW_AGENT_PUSH_TOKEN
  delete process.env.NODE_ENV
}

function buildResponseFrame(payload: Record<string, unknown>): Buffer {
  const body = Buffer.from(JSON.stringify(payload), 'utf8')
  const header = Buffer.alloc(HEADER_SIZE, 0)
  PROTO_MAGIC.copy(header, 0)
  header.writeUInt8(1, 4)
  header.writeUInt8(222, 5)
  header.writeUInt32BE(1, 8)
  header.writeUInt32BE(body.length, 12)
  return Buffer.concat([header, body])
}

function readFrame(socket: net.Socket): Promise<{ header: Buffer; payload: Record<string, unknown> }> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = []
    socket.on('data', (chunk) => {
      chunks.push(chunk)
      const frame = Buffer.concat(chunks)
      if (frame.length < HEADER_SIZE) return
      const payloadLen = frame.readUInt32BE(12)
      if (frame.length < HEADER_SIZE + payloadLen) return
      const body = frame.subarray(HEADER_SIZE, HEADER_SIZE + payloadLen).toString('utf8')
      resolve({ header: frame.subarray(0, HEADER_SIZE), payload: JSON.parse(body) as Record<string, unknown> })
    })
    socket.on('error', reject)
  })
}

describe('openclaw-agent-push-bridge', () => {
  beforeEach(() => {
    clearPushBridgeEnv()
    clearAgentPushIdempotencyCache()
  })

  afterEach(() => {
    clearPushBridgeEnv()
    clearAgentPushIdempotencyCache()
  })

  it('keeps the development fallback token but disables it in production', () => {
    expect(resolveOpenClawAgentPushToken()).toBeTruthy()

    process.env.NODE_ENV = 'production'
    expect(resolveOpenClawAgentPushToken()).toBe('')

    process.env.OPENCLAW_AGENT_PUSH_TOKEN = 'push-token'
    expect(resolveOpenClawAgentPushToken()).toBe('push-token')
  })

  it('does not write the fallback token into production push env', () => {
    process.env.NODE_ENV = 'production'
    process.env.OPENCLAW_AGENT_PUSH_PUBLIC_URL = 'https://agentchat-push.example.test/api/openclaw-agent-push'

    ensureOpenClawAgentPushEnv(3000)

    expect(process.env.AGENTCHAT_OPENCLAW_AGENT_PUSH_URL).toBe('https://agentchat-push.example.test/api/openclaw-agent-push')
    expect(process.env.AGENTCHAT_OPENCLAW_AGENT_PUSH_TOKEN).toBeUndefined()
  })

  it('deduplicates repeated idempotency keys in the bridge process', () => {
    expect(rememberAgentPushIdempotencyKey('same-key', 1000)).toBe(true)
    expect(rememberAgentPushIdempotencyKey('same-key', 1001)).toBe(false)
    expect(rememberAgentPushIdempotencyKey('same-key', 1000 + 6 * 60 * 60 * 1000 + 1)).toBe(true)
    expect(rememberAgentPushIdempotencyKey('', 1002)).toBe(true)
  })

  it('unwraps OpenClaw cron webhook payload strings before validation', () => {
    process.env.OPENCLAW_AGENT_PUSH_TOKEN = 'push-token'

    const resolved = resolveAgentPushRequestBody({
      payload: JSON.stringify({
        kind: 'systemEvent',
        text: JSON.stringify({
          cmd: 'AGENT_PUSH',
          channel: '#group-real',
          message: '提醒：喝水',
          idempotencyKey: 'reminder-1',
          scope: 'personal',
          eventType: 'personal_task_due',
          personalTaskId: 'ptask-1',
          reminderId: 'rem-1',
          creatorUserId: '19521112908',
          deliverTo: { type: 'conversation', channel: '#group-real' },
        }),
      }),
      token: 'push-token',
    })

    expect(resolved.source).toBe('openclaw-cron-webhook')
    expect(validateAgentPushBody(resolved.body)).toMatchObject({
      cmd: 'AGENT_PUSH',
      token: 'push-token',
      channel: '#group-real',
      message: '@19521112908 提醒：喝水',
      idempotencyKey: 'reminder-1',
      scope: 'personal',
      eventType: 'personal_task_due',
      personalTaskId: 'ptask-1',
      reminderId: 'rem-1',
      creatorUserId: '19521112908',
      deliverTo: { type: 'conversation', channel: '#group-real' },
    })
  })

  it('keeps group reminder payloads scoped to the group instead of mentioning only the creator', () => {
    process.env.OPENCLAW_AGENT_PUSH_TOKEN = 'push-token'

    expect(validateAgentPushBody({
      cmd: 'AGENT_PUSH',
      token: 'push-token',
      channel: '#group-real',
      message: '提醒：交作业',
      idempotencyKey: 'group-reminder-1',
      scope: 'group',
      eventType: 'group_reminder_due',
      postId: 'post-1',
      reminderId: 'rem-1',
      creatorUserId: '19521112908',
      deliverTo: { type: 'conversation', channel: '#group-real' },
    })).toMatchObject({
      cmd: 'AGENT_PUSH',
      token: 'push-token',
      channel: '#group-real',
      message: '提醒：交作业',
      idempotencyKey: 'group-reminder-1',
      scope: 'group',
      eventType: 'group_reminder_due',
      postId: 'post-1',
      reminderId: 'rem-1',
      creatorUserId: '19521112908',
      deliverTo: { type: 'conversation', channel: '#group-real' },
    })
  })

  it('preserves group task notification metadata for the upstream server', () => {
    process.env.OPENCLAW_AGENT_PUSH_TOKEN = 'push-token'

    expect(validateAgentPushBody({
      cmd: 'AGENT_PUSH',
      token: 'push-token',
      channel: '#group-real',
      message: '提醒：集合',
      scope: 'group',
      eventType: 'group_task_due',
      postId: 'post-1',
      notifyMembers: true,
      notification: {
        scope: 'group_members',
        title: '群任务提醒',
        body: '提醒：集合',
        postId: 'post-1',
      },
    })).toMatchObject({
      cmd: 'AGENT_PUSH',
      token: 'push-token',
      channel: '#group-real',
      message: '提醒：集合',
      scope: 'group',
      eventType: 'group_task_due',
      postId: 'post-1',
      notifyMembers: true,
      notification: {
        scope: 'group_members',
        title: '群任务提醒',
        postId: 'post-1',
      },
    })
  })

  it('unwraps nested job payload strings from cron webhooks', () => {
    process.env.OPENCLAW_AGENT_PUSH_TOKEN = 'push-token'

    const resolved = resolveAgentPushRequestBody({
      job: {
        payload: JSON.stringify({
          cmd: 'AGENT_PUSH',
          channel: '#group-real',
          message: '提醒：喝水',
        }),
      },
      token: 'push-token',
    })

    expect(resolved.source).toBe('openclaw-cron-webhook')
    expect(validateAgentPushBody(resolved.body)).toMatchObject({
      cmd: 'AGENT_PUSH',
      token: 'push-token',
      channel: '#group-real',
      message: '提醒：喝水',
    })
  })

  it('does not stop at wrapper conversation ids before reading the nested push payload', () => {
    process.env.OPENCLAW_AGENT_PUSH_TOKEN = 'push-token'

    const resolved = resolveAgentPushRequestBody({
      conversationId: '#group-real',
      payload: {
        text: JSON.stringify({
          cmd: 'AGENT_PUSH',
          channel: '#group-real',
          message: '提醒：喝水',
        }),
      },
      token: 'push-token',
    })

    expect(resolved.source).toBe('openclaw-cron-webhook')
    expect(validateAgentPushBody(resolved.body)).toMatchObject({
      cmd: 'AGENT_PUSH',
      token: 'push-token',
      channel: '#group-real',
      message: '提醒：喝水',
    })
  })

  it('sends AGENT_PUSH as AppProto 221 with session_id=0', async () => {
    let received: { header: Buffer; payload: Record<string, unknown> } | undefined
    const server = net.createServer((socket) => {
      void readFrame(socket).then((frame) => {
        received = frame
        socket.write(buildResponseFrame({
          status: 'ok',
          event: 'AGENT_PUSH_ACK',
          channel: frame.payload.channel,
          message: '消息已推送',
          deduplicated: false,
          ts: 1710000000,
        }))
      })
    })

    await new Promise<void>((resolve) => server.listen(0, '127.0.0.1', resolve))
    const address = server.address()
    if (!address || typeof address === 'string') throw new Error('test server did not bind a TCP port')

    try {
      const payload: AgentPushPayload = {
        cmd: 'AGENT_PUSH',
        token: 'push-token',
        channel: '#group-real',
        message: '提醒：喝水',
        idempotencyKey: '#group-real:1710000000:req-1',
        attachments: [],
        agentRequestId: 'req-1',
      }
      const response = await sendAgentPushToChatServer(payload, {
        host: '127.0.0.1',
        port: address.port,
        timeoutMs: 1000,
      })

      expect(response).toMatchObject({ status: 'ok', event: 'AGENT_PUSH_ACK', channel: '#group-real' })
      expect(received?.header.subarray(0, 4).toString('ascii')).toBe('HBPC')
      expect(received?.header.readUInt8(4)).toBe(1)
      expect(received?.header.readUInt8(5)).toBe(221)
      expect(received?.header.readBigUInt64BE(16)).toBe(0n)
      expect(received?.payload).toMatchObject(payload)
    } finally {
      await new Promise<void>((resolve) => server.close(() => resolve()))
    }
  })

  it('rejects obsolete local-demo and legacy personal-workspace channels only', () => {
    process.env.OPENCLAW_AGENT_PUSH_TOKEN = 'push-token'

    expect(() =>
      validateAgentPushBody({
        cmd: 'AGENT_PUSH',
        channel: '#group-posts-demo',
        message: '提醒',
      }),
    ).toThrow(/obsolete channel/)
    expect(() =>
      validateAgentPushBody({
        cmd: 'AGENT_PUSH',
        channel: '#personal-workspace',
        message: '提醒',
      }),
    ).toThrow(/obsolete channel/)
    expect(validateAgentPushBody({
      cmd: 'AGENT_PUSH',
      channel: '#workspace_user-1',
      message: '提醒',
    })).toMatchObject({
      channel: '#workspace_user-1',
      message: '提醒',
    })
  })

  it('uses the public push URL when initializing OpenClaw cron webhook env', () => {
    process.env.OPENCLAW_AGENT_PUSH_PUBLIC_URL = 'https://agentchat-push.example.test/api/openclaw-agent-push'

    ensureOpenClawAgentPushEnv(3000)

    expect(process.env.AGENTCHAT_OPENCLAW_AGENT_PUSH_URL).toBe('https://agentchat-push.example.test/api/openclaw-agent-push')
  })

  it('sendChatServerRequest sends generic payload and returns response', async () => {
    let received: { header: Buffer; payload: Record<string, unknown> } | undefined
    const server = net.createServer((socket) => {
      void readFrame(socket).then((frame) => {
        received = frame
        socket.write(buildResponseFrame({
          status: 'ok',
          event: 'GET_CARD_FILES',
          card_path: 'TV-homework/homework-card/',
          file_count: 0,
          files: [],
        }))
      })
    })

    await new Promise<void>((resolve) => server.listen(0, '127.0.0.1', resolve))
    const address = server.address()
    if (!address || typeof address === 'string') throw new Error('test server did not bind a TCP port')

    try {
      const response = await sendChatServerRequest(
        { cmd: 'GET_CARD_FILES', card_path: 'TV-homework/homework-card/' },
        { host: '127.0.0.1', port: address.port, timeoutMs: 1000 },
      )

      expect(response).toMatchObject({
        status: 'ok',
        event: 'GET_CARD_FILES',
        card_path: 'TV-homework/homework-card/',
        file_count: 0,
      })
      expect(received?.header.subarray(0, 4).toString('ascii')).toBe('HBPC')
      expect(received?.header.readUInt8(4)).toBe(1)
      expect(received?.header.readUInt8(5)).toBe(221)
      expect(received?.header.readBigUInt64BE(16)).toBe(0n)
      expect(received?.payload).toMatchObject({ cmd: 'GET_CARD_FILES', card_path: 'TV-homework/homework-card/' })
    } finally {
      await new Promise<void>((resolve) => server.close(() => resolve()))
    }
  })
})
