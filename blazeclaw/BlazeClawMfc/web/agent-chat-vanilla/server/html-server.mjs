import http from 'node:http'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const PORT = Number(process.env.HTML_PORT || 3000)
const HTML_DIR = path.join(__dirname, '..', 'html')

const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.gif': 'image/gif',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.json': 'application/json; charset=utf-8',
}

function proxyRequest(req, res, targetUrl) {
  const url = new URL(targetUrl)
  const options = {
    hostname: url.hostname,
    port: url.port,
    path: url.pathname + url.search,
    method: req.method,
    headers: {
      ...req.headers,
      host: url.host,
      origin: req.headers.origin || '*',
    },
  }

  const proxyReq = http.request(options, (proxyRes) => {
    res.writeHead(proxyRes.statusCode, proxyRes.headers)
    proxyRes.pipe(res)
  })

  proxyReq.on('error', (e) => {
    console.error(`[html-server] Proxy error to ${targetUrl}:`, e.message)
    res.writeHead(502, { 'Content-Type': 'application/json' })
    res.end(JSON.stringify({ error: 'proxy_error', message: e.message }))
  })

  req.pipe(proxyReq)
}

const server = http.createServer((req, res) => {
  let urlPath = new URL(req.url, `http://${req.headers.host}`).pathname

  // API 代理：/api/chat/* → http://127.0.0.1:8787/*
  if (urlPath.startsWith('/api/chat/')) {
    const backendPath = urlPath.replace(/^\/api\/chat/, '') || '/'
    const backendUrl = `http://127.0.0.1:8787${backendPath}`
    
    proxyRequest(req, res, backendUrl)
    return
  }

  // 认证代理：/api/auth/* → http://127.0.0.1:8787/api/auth/*
  if (urlPath.startsWith('/api/auth/')) {
    const backendUrl = `http://127.0.0.1:8787${urlPath}`
    proxyRequest(req, res, backendUrl)
    return
  }

  // 默认返回 index.html
  if (urlPath === '/' || urlPath === '/index.html') {
    urlPath = '/index.html'
  }

  // 确保路径安全
  const safePath = path.normalize(urlPath).replace(/^(\.\.[/\\])+/, '')
  const filePath = path.join(HTML_DIR, safePath)

  // 检查文件是否存在
  if (!fs.existsSync(filePath)) {
    // 如果文件不存在，返回 index.html（用于 SPA 路由）
    if (!filePath.includes('.')) {
      const indexPath = path.join(HTML_DIR, 'index.html')
      if (fs.existsSync(indexPath)) {
        res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' })
        res.end(fs.readFileSync(indexPath, 'utf8'))
        return
      }
    }
    res.writeHead(404, { 'Content-Type': 'text/plain' })
    res.end('Not Found')
    return
  }

  // 读取文件
  try {
    const ext = path.extname(filePath).toLowerCase()
    const contentType = MIME_TYPES[ext] || 'application/octet-stream'

    res.writeHead(200, {
      'Content-Type': contentType,
      'Cache-Control': 'no-cache',
      'Access-Control-Allow-Origin': '*',
    })
    res.end(fs.readFileSync(filePath))
  } catch (e) {
    res.writeHead(500, { 'Content-Type': 'text/plain' })
    res.end('Internal Server Error')
  }
})

server.listen(PORT, '127.0.0.1', () => {
  console.log(`[html-server] http://127.0.0.1:${PORT}`)
  console.log(`[html-server] Serving from: ${HTML_DIR}`)
})
