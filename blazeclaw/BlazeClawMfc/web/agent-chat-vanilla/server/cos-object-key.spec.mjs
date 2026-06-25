import { describe, expect, it } from 'vitest'
import { buildAgentResourcePathMarker, buildObjectKey, normalizeObjectKeyPrefix, sanitizePathSegment } from './cos-object-key.mjs'

describe('cos object key naming', () => {
  it('keeps legacy homework upload paths when no agent task marker is provided', () => {
    expect(buildObjectKey({
      prefix: 'chat/homework',
      conversationId: '#group-real',
      postId: 'post-1',
      fileName: 'homework.pdf',
      nowMs: 1000,
    })).toBe('chat/homework/group-real/post-1/1000_homework.pdf')
  })

  it('adds source agent and task number markers for AI generated resources', () => {
    expect(buildObjectKey({
      prefix: '/chat/homework/',
      conversationId: '#group-real',
      postId: 'post-1',
      fileName: 'poster final.png',
      taskNo: 'AI20260611-TEST01',
      sourceAgent: 'openclaw',
      nowMs: 1000,
    })).toBe('chat/homework/ai/openclaw/AI20260611-TEST01/group-real/post-1/1000_poster-final.png')
  })

  it('does not add incomplete agent markers without a task number', () => {
    expect(buildObjectKey({
      prefix: 'chat/homework',
      conversationId: '#group-real',
      postId: 'post-1',
      fileName: 'homework.pdf',
      sourceAgent: 'openclaw',
      nowMs: 1000,
    })).toBe('chat/homework/group-real/post-1/1000_homework.pdf')
  })

  it('normalizes path segments and provider aliases consistently', () => {
    expect(normalizeObjectKeyPrefix(' /root/path/ ')).toBe('root/path')
    expect(sanitizePathSegment('#group/with spaces')).toBe('group-with-spaces')
    expect(buildAgentResourcePathMarker({
      providerId: 'future provider',
      taskNo: 'AI/2026 0611',
    })).toBe('ai/future-provider/AI-2026-0611')
  })
})
