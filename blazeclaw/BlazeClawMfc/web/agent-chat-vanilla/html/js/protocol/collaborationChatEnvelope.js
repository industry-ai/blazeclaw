/* ================================================================
   协同聊天信封 - c:agentchat.collaboration 编码/解码
   对齐 origin-vanilla html/js/protocol/collaborationChatEnvelope.js
   对齐 agent/docs/protocols/多端协同协议定义.md §3.1
   ================================================================ */

import { COLLABORATION_PROTOCOL, COLLABORATION_VERSION } from './collaborationProtocol.js';

export const COLLABORATION_CHAT_PREFIX = 'c:agentchat.collaboration';

function _trim(value) {
  return String(value ?? '').trim();
}

function _isRecord(value) {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
}

function _normalizeIdList(value) {
  if (!Array.isArray(value)) return [];
  return value.map(item => _trim(item)).filter(Boolean);
}

/**
 * 编码协同指令为聊天通道文本（对齐协议 §3.1）
 * 格式：c:agentchat.collaboration\n{JSON}
 */
export function encodeCollaborationChatMessage(input) {
  const instruction = {
    ...input.instruction,
    targetNodeIds: _normalizeIdList(input.targetNodeIds),
    targetDeviceIds: _normalizeIdList(input.targetDeviceIds),
  };
  return `${COLLABORATION_CHAT_PREFIX}\n${JSON.stringify(instruction)}`;
}

/**
 * 解析聊天消息中的协同指令（对齐协议 §3.1）
 * @returns {{ instruction, targetNodeIds, targetDeviceIds } | null}
 */
export function parseCollaborationChatMessage(message) {
  const text = _trim(message);
  if (!text.startsWith(`${COLLABORATION_CHAT_PREFIX}\n`)) return null;

  const rawJson = text.slice(COLLABORATION_CHAT_PREFIX.length).trim();
  if (!rawJson) return null;

  try {
    const parsed = JSON.parse(rawJson);
    if (!_isRecord(parsed)) return null;
    if (parsed.protocol !== COLLABORATION_PROTOCOL) return null;
    if (parsed.version !== COLLABORATION_VERSION) return null;
    if (!_trim(parsed.action)) return null;

    return {
      instruction: parsed,
      targetNodeIds: _normalizeIdList(parsed.targetNodeIds),
      targetDeviceIds: _normalizeIdList(parsed.targetDeviceIds),
    };
  } catch {
    return null;
  }
}

/**
 * 判断消息是否是协同协议文本
 */
export function isCollaborationChatMessage(message) {
  return _trim(message).startsWith(`${COLLABORATION_CHAT_PREFIX}\n`);
}

export default {
  COLLABORATION_CHAT_PREFIX,
  encodeCollaborationChatMessage,
  parseCollaborationChatMessage,
  isCollaborationChatMessage,
};
