/**
 * BlazeClaw Agent Bridge — 独立 HTTP 服务入口。
 *
 * 将 agent bridge 和 push bridge 部署为同一个 HTTP 服务，对外暴露端点：
 *   /api/blazeclaw-agent      — 前端 AI 对话请求（非流式 + SSE 流式）
 *   /api/blazeclaw-agent-push — BlazeClaw cron 定时提醒回调
 *   /api/openclaw-agent       — 兼容旧入口
 *   /api/openclaw-agent-push  — 兼容旧 cron 回调
 *
 * 此服务需要部署在 BlazeClaw gateway 同一台机器上，因为：
 *   1. agent bridge 通过 WS 直连 gateway (ws://127.0.0.1:18789)
 *   2. push bridge 的 pushUrl 会被 BlazeClaw cron exec 以 127.0.0.1 请求
 *
 * 用法：
 *   npx tsx server/blazeclaw-agent-server.ts
 *   BLAZECLAW_AGENT_BRIDGE_PORT=8788 npx tsx server/blazeclaw-agent-server.ts
 */
import http from 'node:http'
import { handleOpenClawAgentBridgeRequest } from './openclaw-agent-bridge'
import { ensureOpenClawAgentPushEnv, handleOpenClawAgentPushBridgeRequest } from './openclaw-agent-push-bridge'
import { handleCollaborationRelayRequest } from './collaboration-relay'
import { handleTtsSynthesizeRequest } from './tts-bridge'

const PORT = Number(process.env.BLAZECLAW_AGENT_BRIDGE_PORT || process.env.OPENCLAW_BRIDGE_PORT || 8788)
// 确保 push bridge 所需环境变量已设置
ensureOpenClawAgentPushEnv(PORT)

function notFound(res: http.ServerResponse): void {
  const body = JSON.stringify({ ok: false, error: 'not_found' })
  res.writeHead(404, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body),
    'Access-Control-Allow-Origin': '*',
  })
  res.end(body)
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url ?? '/', `http://${req.headers.host || 'localhost'}`)

  // 健康检查
  if (req.method === 'GET' && url.pathname === '/health') {
    const gateway = process.env.BLAZECLAW_GATEWAY_URL || process.env.OPENCLAW_GATEWAY_URL || 'ws://127.0.0.1:18789'
    const body = JSON.stringify({ ok: true, service: 'blazeclaw-agent-bridge', gateway })
    res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': Buffer.byteLength(body) })
    res.end(body)
    return
  }

  // AI 对话桥接（前端请求 → BlazeClaw gateway）
  if (url.pathname === '/api/blazeclaw-agent' || url.pathname === '/api/openclaw-agent') {
    void handleOpenClawAgentBridgeRequest(req, res)
    return
  }

  // 定时提醒回调（BlazeClaw cron exec PowerShell POST）
  if (url.pathname === '/api/blazeclaw-agent-push' || url.pathname === '/api/openclaw-agent-push') {
    void handleOpenClawAgentPushBridgeRequest(req, res)
    return
  }

  if (url.pathname.startsWith('/api/collaboration')) {
    req.url = url.pathname.replace(/^\/api\/collaboration/, '') + url.search || '/'
    void handleCollaborationRelayRequest(req, res)
    return
  }

  if (url.pathname === '/api/tts/synthesize') {
    void handleTtsSynthesizeRequest(req, res)
    return
  }

  notFound(res)
})

server.listen(PORT, '0.0.0.0', () => {
  console.log(`[blazeclaw-agent-server] listening on http://0.0.0.0:${PORT}`)
  console.log(`[blazeclaw-agent-server] gateway → ${process.env.BLAZECLAW_GATEWAY_URL || process.env.OPENCLAW_GATEWAY_URL || 'ws://127.0.0.1:18789'}`)
  console.log(`[blazeclaw-agent-server] health → http://127.0.0.1:${PORT}/health`)
})
