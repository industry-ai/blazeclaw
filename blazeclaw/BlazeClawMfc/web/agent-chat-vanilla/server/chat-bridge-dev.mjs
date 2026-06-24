/**
 * [AgentChat 文件说明]
 * Node 聊天桥接（开发变体）：本地调试与上游连接参数。
 */
/**
 * Local dev bootstrap for chat-bridge.
 * Sets sensible defaults so you can run one command:
 *   npm run chat-bridge
 */
process.env.CHAT_TLS_REJECT_UNAUTHORIZED ??= '0'
process.env.CHAT_TCP_MODE ??= 'plain'
process.env.CHAT_TCP_PORT ??= '8765'
process.env.CHAT_TLS_PREFLIGHT_ENABLE ??= '0'
// Workaround for backend decoding session_id as swapped 32-bit words.
process.env.CHAT_SESSION_ID_WORD_SWAP ??= '1'
process.env.CHAT_TLS_PREFLIGHT_REQ_TYPE ??= '9'
process.env.CHAT_TLS_PREFLIGHT_RESP_TYPE ??= '24'
process.env.CHAT_TLS_PREFLIGHT_PAYLOAD ??= '{}'

await import('./chat-bridge.mjs')
