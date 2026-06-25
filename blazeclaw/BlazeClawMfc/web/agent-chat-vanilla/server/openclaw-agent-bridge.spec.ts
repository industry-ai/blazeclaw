import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  buildAgentChatGroupTaskCronParams,
  buildAgentChatReminderCronParams,
  buildOpenClawResourceNamingContext,
  createOpenClawDeltaTextTracker,
  extractCardMetadata,
  requesterMentionFromBody,
  sanitizeOpenClawVisibleText,
  shouldInjectCardFilesContext,
  shouldInjectCronContext,
  streamChatEvents,
  tryParseAgentChatGroupTaskIntent,
  tryParseAgentChatReminderIntent,
} from './openclaw-agent-bridge'
import {
  buildOpenClawRunnerMessage,
  fallbackForLowInformationCardResult,
  parseAssistantText,
  waitForTerminalChatEvent,
} from './openclaw-shared'

describe('openclaw-agent-bridge stream delta routing', () => {
  it('converts cumulative message snapshots into append-only deltas', () => {
    const nextDelta = createOpenClawDeltaTextTracker()

    expect(nextDelta({ message: { content: [{ type: 'text', text: 'A' }] } })).toBe('A')
    expect(nextDelta({ message: { content: [{ type: 'text', text: 'AB' }] } })).toBe('B')
    expect(nextDelta({ message: { content: [{ type: 'text', text: 'ABC' }] } })).toBe('C')
    expect(nextDelta({ message: { content: [{ type: 'text', text: 'ABC' }] } })).toBe('')
  })

  it('passes through explicit OpenClaw deltas without replaying message snapshots', () => {
    const nextDelta = createOpenClawDeltaTextTracker()

    expect(nextDelta({
      delta: 'A',
      message: { content: [{ type: 'text', text: 'A' }] },
    })).toBe('A')
    expect(nextDelta({
      delta: 'B',
      message: { content: [{ type: 'text', text: 'AB' }] },
    })).toBe('B')
    expect(nextDelta({ message: { content: [{ type: 'text', text: 'ABC' }] } })).toBe('C')
  })

  it('uses terminal tool results without replaying assistant process text', async () => {
    let listener: ((frame: { type: string; event: string; payload?: Record<string, unknown> }) => void) | undefined
    const unsubscribe = vi.fn()
    const client = {
      addEventListener: vi.fn((next) => {
        listener = next
        return unsubscribe
      }),
    } as unknown as Parameters<typeof waitForTerminalChatEvent>[0]

    const pending = waitForTerminalChatEvent(client, 'run-1', 1000)

    listener?.({
      type: 'event',
      event: 'chat',
      payload: {
        runId: 'run-1',
        state: 'final',
        message: {
          content: [
            { type: 'text', text: 'Let me inspect and create the page.' },
            { type: 'tool_result', tool_use_id: 'tool-1', content: '最终学习卡片：https://example.com/card.html' },
          ],
        },
      },
    })

    await expect(pending).resolves.toMatchObject({
      text: '最终学习卡片：https://example.com/card.html',
    })
    expect(unsubscribe).toHaveBeenCalled()
  })

  it('reads URL text from array tool result content blocks', async () => {
    let listener: ((frame: { type: string; event: string; payload?: Record<string, unknown> }) => void) | undefined
    const unsubscribe = vi.fn()
    const client = {
      addEventListener: vi.fn((next) => {
        listener = next
        return unsubscribe
      }),
    } as unknown as Parameters<typeof waitForTerminalChatEvent>[0]

    const pending = waitForTerminalChatEvent(client, 'run-array-tool-result', 1000)

    listener?.({
      type: 'event',
      event: 'chat',
      payload: {
        runId: 'run-array-tool-result',
        state: 'final',
        message: {
          content: [
            { type: 'text', text: '已处理完成，请查看当前结果。' },
            {
              type: 'tool_result',
              tool_use_id: 'tool-1',
              content: [
                { type: 'text', text: 'H5 卡片已生成，请查看下方链接。' },
                { type: 'text', text: 'https://example.com/homework/unit3-family/index.html' },
              ],
            },
          ],
        },
      },
    })

    await expect(pending).resolves.toMatchObject({
      text: 'H5 卡片已生成，请查看下方链接。\nhttps://example.com/homework/unit3-family/index.html',
    })
    expect(unsubscribe).toHaveBeenCalled()
  })

  it('keeps waiting when OpenClaw enters needs_approval before the final result', async () => {
    let listener: ((frame: { type: string; event: string; payload?: Record<string, unknown> }) => void) | undefined
    const unsubscribe = vi.fn()
    const client = {
      addEventListener: vi.fn((next) => {
        listener = next
        return unsubscribe
      }),
    } as unknown as Parameters<typeof waitForTerminalChatEvent>[0]

    const pending = waitForTerminalChatEvent(client, 'run-approval', 1000)
    let settled = false
    pending.finally(() => { settled = true })

    listener?.({
      type: 'event',
      event: 'chat',
      payload: {
        runId: 'run-approval',
        state: 'needs_approval',
        message: {
          content: [{ type: 'text', text: 'Let me use a tool.' }],
        },
      },
    })

    await Promise.resolve()
    expect(settled).toBe(false)
    expect(unsubscribe).not.toHaveBeenCalled()

    listener?.({
      type: 'event',
      event: 'chat',
      payload: {
        runId: 'run-approval',
        state: 'final',
        message: {
          content: [{ type: 'text', text: 'https://example.com/final-card.html' }],
        },
      },
    })

    await expect(pending).resolves.toMatchObject({
      text: 'https://example.com/final-card.html',
    })
    expect(unsubscribe).toHaveBeenCalledTimes(1)
  })

  it('finishes the stream with tool result content when OpenClaw times out after producing a URL', async () => {
    vi.useFakeTimers()
    let listener: ((frame: { type: string; event: string; payload?: Record<string, unknown> }) => void) | undefined
    const unsubscribe = vi.fn()
    const client = {
      addEventListener: vi.fn((next) => {
        listener = next
        return unsubscribe
      }),
    } as unknown as Parameters<typeof streamChatEvents>[0]
    const writes: string[] = []
    const res = {
      write: vi.fn((chunk: string) => {
        writes.push(chunk)
        return true
      }),
    } as unknown as Parameters<typeof streamChatEvents>[3]

    try {
      const pending = streamChatEvents(client, 'run-timeout-card', 50, res)

      listener?.({
        type: 'event',
        event: 'chat',
        payload: {
          runId: 'run-timeout-card',
          state: 'running',
          message: {
            content: [
              {
                type: 'tool_result',
                tool_use_id: 'tool-1',
                content: '路演 H5 卡片已生成并上线:\nhttps://example.com/roadshow_card.html',
              },
            ],
          },
        },
      })

      await vi.advanceTimersByTimeAsync(60)
      await expect(pending).resolves.toBeUndefined()
      const output = writes.join('\n')
      expect(output).toContain('"type":"tool_result"')
      expect(output).toContain('"type":"final"')
      expect(output).toContain('https://example.com/roadshow_card.html')
      expect(output).not.toContain('"type":"error"')
      expect(unsubscribe).toHaveBeenCalled()
    } finally {
      vi.useRealTimers()
    }
  })

  it('recovers trajectory final text when the terminal event only has a completion placeholder', async () => {
    const runId = `agent-chat-test-${Date.now()}`
    const sessionsDir = path.join(os.homedir(), '.openclaw', 'agents', 'main', 'sessions')
    const trajectoryPath = path.join(sessionsDir, `${runId}.trajectory.jsonl`)
    fs.mkdirSync(sessionsDir, { recursive: true })

    let listener: ((frame: { type: string; event: string; payload?: Record<string, unknown> }) => void) | undefined
    const unsubscribe = vi.fn()
    const client = {
      addEventListener: vi.fn((next) => {
        listener = next
        return unsubscribe
      }),
    } as unknown as Parameters<typeof waitForTerminalChatEvent>[0]

    try {
      const pending = waitForTerminalChatEvent(client, runId, 1000)
      fs.writeFileSync(trajectoryPath, `${JSON.stringify({
        type: 'trace.artifacts',
        runId,
        data: {
          assistantTexts: [
            'HTML generated. Now upload to COS.',
            '路演 H5 卡片已生成并可访问：\n\nhttps://example.com/card.html',
          ],
        },
      })}\n`, 'utf8')

      listener?.({
        type: 'event',
        event: 'chat',
        payload: {
          runId,
          state: 'final',
          message: {
            content: [
              { type: 'text', text: '已处理完成，请查看当前结果。' },
            ],
          },
        },
      })

      await expect(pending).resolves.toMatchObject({
        text: '路演 H5 卡片已生成并可访问：\n\nhttps://example.com/card.html',
      })
      expect(unsubscribe).toHaveBeenCalled()
    } finally {
      fs.rmSync(trajectoryPath, { force: true })
    }
  })

  it('recovers trajectory URL when image final text omits the public link', async () => {
    const runId = `agent-chat-image-test-${Date.now()}`
    const sessionsDir = path.join(os.homedir(), '.openclaw', 'agents', 'main', 'sessions')
    const trajectoryPath = path.join(sessionsDir, `${runId}.trajectory.jsonl`)
    fs.mkdirSync(sessionsDir, { recursive: true })

    let listener: ((frame: { type: string; event: string; payload?: Record<string, unknown> }) => void) | undefined
    const unsubscribe = vi.fn()
    const client = {
      addEventListener: vi.fn((next) => {
        listener = next
        return unsubscribe
      }),
    } as unknown as Parameters<typeof waitForTerminalChatEvent>[0]

    try {
      const publicUrl = 'https://corp.blazegraph.site/ai/openclaw/AI20260618-44WY2G/cat.png'
      const pending = waitForTerminalChatEvent(client, runId, 1000)
      fs.writeFileSync(trajectoryPath, `${JSON.stringify({
        type: 'trace.artifacts',
        runId,
        data: {
          assistantTexts: [
            '已生成小猫图片。',
            `已生成小猫图片。\n\nMEDIA:${publicUrl}`,
          ],
        },
      })}\n`, 'utf8')

      listener?.({
        type: 'event',
        event: 'chat',
        payload: {
          runId,
          state: 'final',
          message: {
            content: [
              { type: 'text', text: '已生成小猫图片。' },
              {
                type: 'image',
                url: '/api/chat/media/outgoing/session/image/full',
                openUrl: '/api/chat/media/outgoing/session/image/full',
              },
            ],
          },
        },
      })

      await expect(pending).resolves.toMatchObject({
        text: `已生成小猫图片。\n\nMEDIA:${publicUrl}`,
      })
      expect(unsubscribe).toHaveBeenCalled()
    } finally {
      fs.rmSync(trajectoryPath, { force: true })
    }
  })

  it('preserves image content block URLs while parsing assistant text', () => {
    expect(parseAssistantText({
      content: [
        { type: 'text', text: '已生成小猫图片。' },
        { type: 'image', url: 'https://example.com/cat.png' },
      ],
    })).toBe('已生成小猫图片。\nhttps://example.com/cat.png')
  })
})

describe('openclaw-agent-bridge requester mentions', () => {
  it('prefers phone fields over raw user ids', () => {
    expect(requesterMentionFromBody({
      userId: 'cb2ebe58-e2fd-4e2f-a38f-33b6bfcf475f',
      userPhone: '19521112908',
    })).toBe('@19521112908')
    expect(requesterMentionFromBody({
      creatorUserId: 'cb2ebe58-e2fd-4e2f-a38f-33b6bfcf475f',
    })).toBe('@cb2ebe58-e2fd-4e2f-a38f-33b6bfcf475f')
  })
})

describe('openclaw runner card request protocol', () => {
  it('wraps group card generation requests with URL-return requirements', () => {
    const message = buildOpenClawRunnerMessage({
      message: '生成外研版3上(P10-13)英语家庭作业',
      conversationId: '#group-1',
      groupId: '#group-1',
      messageId: 'msg-1',
      userPhone: '13260610930',
    })

    expect(message).toContain('AI_TASK_REQUEST:')
    expect(message).toContain('Card generation requirement:')
    expect(message).toContain('public http/https .html URL')
    expect(message).toContain('生成外研版3上(P10-13)英语家庭作业')
  })

  it('does not treat low-information card results as successful replies', () => {
    expect(
      fallbackForLowInformationCardResult(
        '已处理完成，请查看当前结果。',
        '生成外研版3上(P10-13)英语家庭作业',
      ),
    ).toBe('卡片生成没有返回可打开链接，请稍后重试。')
  })
})

describe('openclaw-agent-bridge reminder cron routing', () => {
  afterEach(() => {
    vi.unstubAllEnvs()
  })

  it('parses Chinese relative personal reminders only', () => {
    expect(tryParseAgentChatReminderIntent('@炎图AI助手 两分钟后提醒我喝水')).toMatchObject({
      delayMs: 120000,
      delayLabel: '两分钟后',
      reminderText: '喝水',
    })
    expect(tryParseAgentChatReminderIntent('@炎图AI助手 10分钟后提醒我出门')).toMatchObject({
      delayMs: 600000,
      reminderText: '出门',
    })
    expect(tryParseAgentChatReminderIntent('@炎图AI助手 10分钟后提醒大家交作业')).toBeNull()
  })

  it('parses group scheduled task reminders without using the personal reminder path', () => {
    const now = new Date(2026, 5, 3, 9, 0, 0).getTime()

    expect(tryParseAgentChatReminderIntent('@炎图AI助手 提醒大家明天早上六点集合')).toBeNull()
    expect(tryParseAgentChatGroupTaskIntent('@炎图AI助手 提醒大家明天早上六点集合', now)).toMatchObject({
      delayLabel: '明天早上六点',
      reminderText: '集合',
      title: '集合',
    })
    expect(tryParseAgentChatGroupTaskIntent('@炎图AI助手 提醒大家18:40集合', now)).toMatchObject({
      delayLabel: '18:40',
      reminderText: '集合',
      title: '集合',
      triggerAtMs: new Date(2026, 5, 3, 18, 40, 0).getTime(),
    })
    expect(tryParseAgentChatGroupTaskIntent('@炎图AI助手 明天早上九点设置运动会提醒', now)).toMatchObject({
      delayLabel: '明天早上九点',
      reminderText: '运动会',
      title: '运动会',
      triggerAtMs: new Date(2026, 5, 4, 9, 0, 0).getTime(),
    })
  })

  it('does not treat publish notice commands as scheduled group reminders', () => {
    const now = new Date(2026, 5, 3, 15, 8, 0).getTime()

    expect(tryParseAgentChatGroupTaskIntent(
      '@炎图AI助手 发布一条通知：今天下午三点进行系统升级，请大家提前保存数据',
      now,
    )).toBeNull()
  })

  it('removes OpenClaw execution details from user-visible replies', () => {
    expect(sanitizeOpenClawVisibleText([
      'Let me get the correct base64 first, then create the cron job.',
      'Now let me fix the delivery mode to none since this uses webhook push.',
      '✅ 已为您设置运动会提醒！',
      '',
      '详情：',
      '时间：明天早上 9:00',
      '消息：请准时参加运动会。',
    ].join('\n'))).toBe([
      '已为您设置运动会提醒！',
      '',
      '详情：',
      '时间：明天早上 9:00',
      '消息：请准时参加运动会。',
    ].join('\n'))

    expect(sanitizeOpenClawVisibleText('鑰嘿踩箘氯滑十核泪?锛硅坤塞娩紬旋疯风')).toBe('已处理完成。')
  })

  it('removes Chinese OpenClaw process narration from final skill answers', () => {
    expect(sanitizeOpenClawVisibleText([
      '炎图知识库系统目前由于底层模型依赖（HuggingFace 离线）暂时无法正常检索。',
      '让我尝试从本地已有的知识文档或记忆中查找相关信息。找到了！',
      '根据炎图科技内部福利文档，餐补标准如下：工作日午餐补贴 25 元/天。',
    ].join(''))).toBe('根据炎图科技内部福利文档，餐补标准如下：工作日午餐补贴 25 元/天。')
  })

  it('preserves structured AI card payloads for frontend attachment rendering', () => {
    const payload = JSON.stringify({
      templateId: 'homework_reminder',
      title: '\u4eca\u65e5\u4f5c\u4e1a',
      subjects: ['\u8bed\u6587', '\u6570\u5b66', '\u82f1\u8bed', '\u79d1\u5b66'],
      items: [
        { subject: '\u8bed\u6587', content: '\u4f5c\u4e1a\u6e05\u5355' },
      ],
      actionText: '\u67e5\u770b\u5168\u90e8\u4f5c\u4e1a',
    })

    expect(sanitizeOpenClawVisibleText(payload, 'fallback')).toBe(payload)
  })

  it('builds a one-shot OpenClaw cron job that POSTs AGENT_PUSH directly via agentTurn', () => {
    vi.stubEnv('OPENCLAW_AGENT_PUSH_TOKEN', 'push-token')
    vi.stubEnv('OPENCLAW_AGENT_PUSH_BRIDGE_URL', 'http://127.0.0.1:3000/api/openclaw-agent-push')

    const params = buildAgentChatReminderCronParams({
      conversationId: '#group-7c10ecab',
      messageId: 'msg-1',
      userId: '19521112908',
      personalTaskId: 'ptask-1',
      reminderId: 'rem-1',
    }, {
      delayMs: 120000,
      delayLabel: '两分钟后',
      reminderText: '喝水',
    }, Date.UTC(2026, 4, 26, 2, 0, 0))

    expect(params).toMatchObject({
      enabled: true,
      deleteAfterRun: true,
      schedule: {
        kind: 'at',
        at: '2026-05-26T02:02:00.000Z',
      },
      sessionTarget: 'isolated',
      wakeMode: 'now',
      payload: {
        kind: 'agentTurn',
        timeoutSeconds: 30,
        toolsAllow: ['exec'],
      },
      delivery: {
        mode: 'none',
      },
    })

    const payload = params?.payload as { message?: string }
    const message = payload.message ?? ''
    expect(message).toContain('Invoke-RestMethod')
    expect(message).toContain('http://127.0.0.1:3000/api/openclaw-agent-push')
    // The push JSON is base64-encoded in the PowerShell command
    const b64Match = message.match(/FromBase64String\('([^']+)'\)/)
    expect(b64Match).not.toBeNull()
    const decoded = JSON.parse(Buffer.from(b64Match![1]!, 'base64').toString('utf8'))
    expect(decoded).toMatchObject({
      cmd: 'AGENT_PUSH',
      token: 'push-token',
      channel: '#group-7c10ecab',
      message: '@19521112908 提醒：喝水',
      attachments: [],
      scope: 'personal',
      eventType: 'personal_task_due',
      personalTaskId: 'ptask-1',
      reminderId: 'rem-1',
      creatorUserId: '19521112908',
      conversationId: '#group-7c10ecab',
      deliverTo: { type: 'conversation', channel: '#group-7c10ecab' },
    })
  })

  it('builds group task cron payloads with post id and notification metadata', () => {
    vi.stubEnv('OPENCLAW_AGENT_PUSH_TOKEN', 'push-token')
    vi.stubEnv('OPENCLAW_AGENT_PUSH_BRIDGE_URL', 'http://127.0.0.1:3000/api/openclaw-agent-push')

    const params = buildAgentChatGroupTaskCronParams({
      conversationId: '#group-real',
      messageId: 'msg-group-task',
      userId: '19521112908',
    }, {
      delayMs: 120000,
      delayLabel: '两分钟后',
      reminderText: '交作业',
      title: '交作业',
      triggerAtMs: Date.UTC(2026, 4, 26, 2, 2, 0),
    }, 'post-1')

    expect(params).toMatchObject({
      enabled: true,
      deleteAfterRun: true,
      schedule: {
        kind: 'at',
        at: '2026-05-26T02:02:00.000Z',
      },
      payload: {
        kind: 'agentTurn',
        toolsAllow: ['exec'],
      },
      delivery: {
        mode: 'none',
      },
    })

    const payload = params?.payload as { message?: string }
    const b64Match = (payload.message ?? '').match(/FromBase64String\('([^']+)'\)/)
    expect(b64Match).not.toBeNull()
    const decoded = JSON.parse(Buffer.from(b64Match![1]!, 'base64').toString('utf8'))
    expect(decoded).toMatchObject({
      cmd: 'AGENT_PUSH',
      token: 'push-token',
      idempotencyKey: expect.stringContaining('openclaw:group-task:#group-real:2026-05-26T02:02:00.000Z:msg-group-task'),
      channel: '#group-real',
      message: '提醒：交作业',
      attachments: [
        expect.objectContaining({
          type: 'native_post',
          postId: 'post-1',
          title: '交作业',
          taskKind: 'task',
          actionType: 'create',
          resourceType: 'task',
          deadlineAt: Date.UTC(2026, 4, 26, 2, 2, 0),
        }),
      ],
      scope: 'group',
      eventType: 'group_task_due',
      postId: 'post-1',
      creatorUserId: '19521112908',
      notifyMembers: true,
      notification: {
        scope: 'group_members',
        title: '群任务提醒',
        postId: 'post-1',
      },
    })
  })

  it('does not build stale group task cron payloads when the trigger time has already passed', () => {
    vi.stubEnv('OPENCLAW_AGENT_PUSH_TOKEN', 'push-token')
    vi.stubEnv('OPENCLAW_AGENT_PUSH_BRIDGE_URL', 'http://127.0.0.1:3000/api/openclaw-agent-push')

    const params = buildAgentChatGroupTaskCronParams({
      conversationId: '#group-real',
      messageId: 'msg-stale-group-task',
      userId: '19521112908',
    }, {
      delayMs: 120000,
      delayLabel: '今天下午三点',
      reminderText: '进行系统升级，请大家提前保存数据',
      title: '系统升级',
      triggerAtMs: Date.UTC(2026, 5, 3, 7, 0, 0),
    }, 'post-stale', Date.UTC(2026, 5, 3, 7, 8, 0))

    expect(params).toBeNull()
  })

  it('uses public push URL in the agentTurn message', () => {
    vi.stubEnv('OPENCLAW_AGENT_PUSH_TOKEN', 'push-token')
    vi.stubEnv('OPENCLAW_AGENT_PUSH_PUBLIC_URL', 'https://agentchat-push.example.test/api/openclaw-agent-push')

    const params = buildAgentChatReminderCronParams({
      conversationId: '#group-7c10ecab',
      messageId: 'msg-public-url',
    }, {
      delayMs: 120000,
      delayLabel: '两分钟后',
      reminderText: '喝水',
    }, Date.UTC(2026, 4, 26, 2, 0, 0))

    expect(params?.delivery).toMatchObject({ mode: 'none' })
    expect(params?.sessionTarget).toBe('isolated')
    const payload = params?.payload as { message?: string }
    expect(payload.message).toContain('https://agentchat-push.example.test/api/openclaw-agent-push')
  })

  it('does not build cron params for obsolete demo rooms but allows real workspace rooms', () => {
    const intent = {
      delayMs: 120000,
      delayLabel: '两分钟后',
      reminderText: '喝水',
    }

    expect(buildAgentChatReminderCronParams({ conversationId: '#group-posts-demo' }, intent)).toBeNull()
    expect(buildAgentChatReminderCronParams({ conversationId: '#personal-workspace' }, intent)).toBeNull()
    expect(buildAgentChatReminderCronParams({ conversationId: '#workspace_user-1' }, intent)).toMatchObject({
      enabled: true,
      schedule: { kind: 'at' },
    })
  })
})

describe('openclaw-agent-bridge context injection', () => {
  it('builds a resource naming context from task and provider metadata', () => {
    const context = buildOpenClawResourceNamingContext({
      taskNo: 'AI20260611-TEST01',
      providerId: 'openclaw',
    })

    expect(context).toContain('Resource upload hint')
    expect(context).toContain('ai/openclaw/AI20260611-TEST01')
  })

  it('does not duplicate resource naming context when the structured field exists', () => {
    const context = buildOpenClawResourceNamingContext({
      taskNo: 'AI20260611-OLD',
      providerId: 'openclaw',
      resourceNaming: {
        sourceAgent: 'future-agent',
        taskNo: 'AI20260611-NEW',
        pathMarker: 'ai/future-agent/AI20260611-NEW',
        objectKeyPrefix: 'custom/prefix',
        fileNamePrefix: 'AI20260611-NEW_',
      },
    })

    expect(context).toBe('')
  })

  it('does not inject reminder or existing-card context for explicit card generation', () => {
    const body = {
      rawMessage: '帮我生成一个作业提醒卡片：标题是今日作业',
      message: 'wrapped prompt includes reminder instructions',
    }

    expect(shouldInjectCronContext(body)).toBe(false)
    expect(shouldInjectCardFilesContext(body)).toBe(false)
    expect(shouldInjectCardFilesContext({
      rawMessage: '帮我看看有哪些卡片模板可以用',
    })).toBe(false)
  })

  it('still injects focused cron context for real reminders', () => {
    expect(shouldInjectCronContext({
      rawMessage: '十分钟后提醒我喝水',
    })).toBe(true)
  })

  it('keeps card file context disabled unless explicitly enabled', () => {
    expect(shouldInjectCardFilesContext({
      rawMessage: '今天英语作业是什么',
    })).toBe(false)

    vi.stubEnv('OPENCLAW_ENABLE_CARD_FILES_CONTEXT', 'true')
    expect(shouldInjectCardFilesContext({
      rawMessage: '今天英语作业是什么',
    })).toBe(true)
  })
})

describe('card files metadata extraction', () => {
  it('extracts card metadata from GET_CARD_FILES response', () => {
    const response = {
      status: 'ok',
      event: 'GET_CARD_FILES',
      card_path: 'TV-homework/homework-card/',
      file_count: 4,
      files: [
        {
          file_key: 'TV-homework/homework-card/index.html',
          file_type: 'html' as const,
          cdn_url: 'https://static.blazegraph.site/TV-homework/homework-card/index.html',
          content: null,
        },
        {
          file_key: 'TV-homework/homework-card/ai-card.css',
          file_type: 'css' as const,
          cdn_url: null,
          content: '.ai-card { border: 1px solid #ccc; }',
        },
        {
          file_key: 'TV-homework/homework-card/chinese.json',
          file_type: 'json' as const,
          cdn_url: null,
          content: {
            card_type: 'h5_entry',
            title: '语文作业',
            target_url: 'https://example.com/chinese',
            summary: '第三章课后习题',
            template: 'homework',
          },
        },
        {
          file_key: 'TV-homework/homework-card/math.json',
          file_type: 'json' as const,
          cdn_url: null,
          content: {
            card_type: 'h5_entry',
            title: '数学作业',
            target_url: 'https://example.com/math',
            summary: '第五章课后习题',
          },
        },
      ],
    }

    const result = extractCardMetadata(response)

    expect(result.indexHtmlCdnUrl).toBe('https://static.blazegraph.site/TV-homework/homework-card/index.html')
    expect(result.cards).toHaveLength(2)
    expect(result.cards[0]).toMatchObject({
      title: '语文作业',
      summary: '第三章课后习题',
      target_url: 'https://example.com/chinese',
      template: 'homework',
      card_type: 'h5_entry',
    })
    expect(result.cards[1]).toMatchObject({
      title: '数学作业',
      summary: '第五章课后习题',
      target_url: 'https://example.com/math',
      template: 'homework',
    })
  })

  it('skips JSON entries without title', () => {
    const response = {
      status: 'ok',
      event: 'GET_CARD_FILES',
      card_path: 'TV-homework/homework-card/',
      file_count: 2,
      files: [
        {
          file_key: 'TV-homework/homework-card/empty.json',
          file_type: 'json' as const,
          cdn_url: null,
          content: { template: 'news' },
        },
        {
          file_key: 'TV-homework/homework-card/valid.json',
          file_type: 'json' as const,
          cdn_url: null,
          content: { title: '通知', summary: '家长会通知', template: 'news' },
        },
      ],
    }

    const result = extractCardMetadata(response)

    expect(result.cards).toHaveLength(1)
    expect(result.cards[0].title).toBe('通知')
  })

  it('returns null indexHtmlCdnUrl when no index.html found', () => {
    const response = {
      status: 'ok',
      event: 'GET_CARD_FILES',
      card_path: 'some-path/',
      file_count: 1,
      files: [
        {
          file_key: 'some-path/math.json',
          file_type: 'json' as const,
          cdn_url: null,
          content: { title: '数学作业' },
        },
      ],
    }

    const result = extractCardMetadata(response)
    expect(result.indexHtmlCdnUrl).toBeNull()
    expect(result.cards).toHaveLength(1)
  })

  it('handles empty files array', () => {
    const result = extractCardMetadata({
      status: 'ok',
      event: 'GET_CARD_FILES',
      card_path: 'empty/',
      file_count: 0,
      files: [],
    })

    expect(result.cards).toHaveLength(0)
    expect(result.indexHtmlCdnUrl).toBeNull()
  })
})
