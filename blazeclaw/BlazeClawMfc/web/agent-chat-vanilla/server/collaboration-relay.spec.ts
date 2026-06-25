import { beforeEach, describe, expect, it } from 'vitest'
import {
  acceptCollaborationInstruction,
  getCurrentDisplay,
  resetCollaborationRelayForTest,
} from './collaboration-relay'

const NOW = 1_800_000_000_000

function instruction(patch: Record<string, unknown> = {}) {
  return {
    protocol: 'agentchat.collaboration',
    version: 1,
    action: 'device.open_content',
    conversationId: '#roadshow-room',
    dispatchId: 'dispatch-1',
    dedupeKey: 'open:#roadshow-room:result-1',
    timestamp: NOW,
    ttlMs: 30_000,
    payload: {
      contentType: 'webview',
      title: 'AI 处理结果',
      url: 'https://example.com/result.html',
      related: {
        taskNo: 'AI20260608-0001',
      },
      bizPayload: {
        ownerDefined: true,
      },
    },
    ...patch,
  }
}

describe('collaboration relay', () => {
  beforeEach(() => {
    resetCollaborationRelayForTest()
  })

  it('accepts standard open content instructions and updates current display', () => {
    const result = acceptCollaborationInstruction(instruction(), NOW)

    expect(result).toMatchObject({
      ok: true,
      status: 'accepted',
      currentDisplay: {
        conversationId: '#roadshow-room',
        displayId: 'dispatch-1',
        title: 'AI 处理结果',
        contentType: 'webview',
        url: 'https://example.com/result.html',
        related: {
          taskNo: 'AI20260608-0001',
        },
      },
    })
    expect(getCurrentDisplay('#roadshow-room')).toMatchObject({
      title: 'AI 处理结果',
      url: 'https://example.com/result.html',
    })
  })

  it('deduplicates repeated instructions without changing the current display', () => {
    acceptCollaborationInstruction(instruction(), NOW)
    const duplicated = acceptCollaborationInstruction(instruction({
      payload: {
        contentType: 'webview',
        title: '重复内容',
        url: 'https://example.com/duplicated.html',
      },
    }), NOW + 100)

    expect(duplicated).toMatchObject({
      ok: true,
      status: 'deduplicated',
      code: 'DUPLICATED',
      currentDisplay: {
        title: 'AI 处理结果',
      },
    })
  })

  it('rejects unsupported actions before any business parsing', () => {
    const result = acceptCollaborationInstruction(instruction({
      action: 'homework.open',
    }), NOW)

    expect(result).toMatchObject({
      ok: false,
      status: 'failed',
      code: 'UNSUPPORTED_ACTION',
    })
    expect(getCurrentDisplay('#roadshow-room')).toBeUndefined()
  })

  it('accepts speak instructions as display text for fixed screen fallback', () => {
    const result = acceptCollaborationInstruction(instruction({
      action: 'device.speak',
      dispatchId: 'speak-1',
      dedupeKey: 'speak:#roadshow-room:AI20260608-0001',
      payload: {
        text: 'AI 结果已生成，请查看大屏。',
        displayText: 'AI 结果已生成',
        bizPayload: {
          ignoredByRelay: true,
        },
      },
    }), NOW)

    expect(result).toMatchObject({
      ok: true,
      status: 'accepted',
      currentDisplay: {
        contentType: 'text',
        title: 'AI 结果已生成',
        speakText: 'AI 结果已生成，请查看大屏。',
      },
    })
  })
})
