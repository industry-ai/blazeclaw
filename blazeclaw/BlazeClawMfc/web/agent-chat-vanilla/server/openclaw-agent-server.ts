/**
 * OpenClaw Agent Bridge — 独立 HTTP 服务入口。
 *
 * 将 agent bridge 和 push bridge 部署为同一个 HTTP 服务，对外暴露两个端点：
 *   /api/openclaw-agent      — 前端 AI 对话请求（非流式 + SSE 流式）
 *   /api/openclaw-agent-push — OpenClaw cron 定时提醒回调
 *
 * 此服务需要部署在 OpenClaw gateway 同一台机器上，因为：
 *   1. agent bridge 通过 WS 直连 gateway (ws://127.0.0.1:18789)
 *   2. push bridge 的 pushUrl 会被 OpenClaw exec 以 127.0.0.1 请求
 *
 * 用法：
 *   npx tsx server/openclaw-agent-server.ts
 *   OPENCLAW_BRIDGE_PORT=8788 npx tsx server/openclaw-agent-server.ts
 */
import http from 'node:http'
import { handleOpenClawAgentBridgeRequest } from './openclaw-agent-bridge'
import { ensureOpenClawAgentPushEnv, handleOpenClawAgentPushBridgeRequest } from './openclaw-agent-push-bridge'
import { handleCollaborationRelayRequest } from './collaboration-relay'
import { handleTtsSynthesizeRequest } from './tts-bridge'

const PORT = Number(process.env.OPENCLAW_BRIDGE_PORT || 8788)
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
    const body = JSON.stringify({ ok: true, service: 'openclaw-agent-bridge', gateway: process.env.OPENCLAW_GATEWAY_URL || 'ws://127.0.0.1:18789' })
    res.writeHead(200, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': Buffer.byteLength(body) })
    res.end(body)
    return
  }

  // AI 对话桥接（前端请求 → OpenClaw gateway）
  if (url.pathname === '/api/openclaw-agent') {
    void handleOpenClawAgentBridgeRequest(req, res)
    return
  }

  // 定时提醒回调（OpenClaw cron exec PowerShell POST）
  if (url.pathname === '/api/openclaw-agent-push') {
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
  console.log(`[openclaw-agent-server] listening on http://0.0.0.0:${PORT}`)
  console.log(`[openclaw-agent-server] gateway → ${process.env.OPENCLAW_GATEWAY_URL || 'ws://127.0.0.1:18789'}`)
  console.log(`[openclaw-agent-server] health → http://127.0.0.1:${PORT}/health`)
})
