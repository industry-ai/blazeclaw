/* ================================================================
   协同协议模块 - CollaborationInstruction 常量与校验
   对齐 origin-vanilla html/js/protocol/collaborationProtocol.js
   对齐 agent/docs/protocols/多端协同协议定义.md §3-§4
   ================================================================ */

// ── 协议常量 ──

export const COLLABORATION_PROTOCOL = 'agentchat.collaboration';
export const COLLABORATION_VERSION = 1;

// ── Action 白名单（对齐协议 §4）──

export const COLLABORATION_ACTIONS = {
  DEVICE_OPEN_CONTENT: 'device.open_content',
  DEVICE_SPEAK: 'device.speak',
  AI_TASK_REQUEST: 'ai.task_request',
  AI_TASK_STATUS: 'ai.task_status',
  AI_SKILL_RESULT: 'ai.skill_result',
  INTERACTIVE_RESOURCE_EVENT: 'interactive.resource_event',
};

// ── 校验函数 ──

/**
 * 校验指令是否过期（对齐协议 §3.5 ttlMs）
 */
export function isExpired(instruction, now = Date.now()) {
  const ttlMs = Number(instruction?.ttlMs);
  if (!Number.isFinite(ttlMs) || ttlMs <= 0) return false;
  const timestamp = Number(instruction?.timestamp);
  if (!Number.isFinite(timestamp) || timestamp <= 0) return false;
  return timestamp + ttlMs < now;
}

export default {
  COLLABORATION_PROTOCOL,
  COLLABORATION_VERSION,
  COLLABORATION_ACTIONS,
  isExpired,
};
