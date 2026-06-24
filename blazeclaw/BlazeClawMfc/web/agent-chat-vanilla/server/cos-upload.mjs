/**
 * [AgentChat 文件说明]
 * 腾讯云 COS 上传服务：接收 multipart 并写入对象存储（供作业/附件等）。
 */
import http from 'node:http'
import Busboy from 'busboy'
import COS from 'cos-nodejs-sdk-v5'
import { buildObjectKey, normalizeObjectKeyPrefix } from './cos-object-key.mjs'

const HOST = process.env.COS_UPLOAD_HOST ?? '127.0.0.1'
const PORT = Number(process.env.COS_UPLOAD_PORT ?? '8790')
const SECRET_ID = String(process.env.COS_SECRET_ID ?? '').trim()
const SECRET_KEY = String(process.env.COS_SECRET_KEY ?? '').trim()
const REGION = String(process.env.COS_REGION ?? '').trim()
const BUCKET = String(process.env.COS_BUCKET ?? '').trim()
const PREFIX = normalizeObjectKeyPrefix()
const MAX_FILE_SIZE_BYTES = Number(process.env.COS_UPLOAD_MAX_FILE_SIZE_BYTES ?? `${20 * 1024 * 1024}`)
const PUBLIC_BASE_URL = String(process.env.COS_PUBLIC_BASE_URL ?? '').trim().replace(/\/$/, '')

const cos =
  SECRET_ID && SECRET_KEY
    ? new COS({
        SecretId: SECRET_ID,
        SecretKey: SECRET_KEY,
      })
    : null

function json(res, status, body) {
  if (res.writableEnded) return
  const text = JSON.stringify(body)
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(text, 'utf8'),
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Headers': 'Content-Type',
    'Access-Control-Allow-Methods': 'GET,POST,OPTIONS',
    Connection: 'close',
  })
  res.end(text)
}

function publicObjectUrl(objectKey) {
  if (PUBLIC_BASE_URL) {
    return `${PUBLIC_BASE_URL}/${encodeURI(objectKey)}`
  }
  if (!BUCKET || !REGION) return ''
  return `https://${BUCKET}.cos.${REGION}.myqcloud.com/${encodeURI(objectKey)}`
}

function inferUploadMimeType(fileName, reportedMimeType) {
  const mimeType = String(reportedMimeType ?? '').trim()
  const lowerName = String(fileName ?? '').trim().toLowerCase()
  if (mimeType && mimeType !== 'application/octet-stream') return mimeType
  if (/\.html?$/.test(lowerName)) return 'text/html; charset=utf-8'
  if (/\.css$/.test(lowerName)) return 'text/css; charset=utf-8'
  if (/\.(?:js|mjs)$/.test(lowerName)) return 'text/javascript; charset=utf-8'
  if (/\.json$/.test(lowerName)) return 'application/json; charset=utf-8'
  if (/\.svg$/.test(lowerName)) return 'image/svg+xml'
  return mimeType || 'application/octet-stream'
}

function shouldServeInline(mimeType) {
  return /^(?:text\/html|text\/css|text\/javascript|application\/javascript|application\/json|image\/svg\+xml)\b/i.test(String(mimeType ?? ''))
}

function parseMultipart(req) {
  return new Promise((resolve, reject) => {
    const fields = {}
    let fileBuffer = null
    let fileName = ''
    let mimeType = 'application/octet-stream'
    let size = 0
    let gotFile = false

    const busboy = Busboy({
      headers: req.headers,
      limits: {
        files: 1,
        fileSize: MAX_FILE_SIZE_BYTES,
      },
    })

    busboy.on('field', (name, value) => {
      fields[name] = String(value ?? '').trim()
    })

    busboy.on('file', (name, stream, info) => {
      if (name !== 'file') {
        stream.resume()
        return
      }
      gotFile = true
      fileName = info.filename || 'upload.bin'
      mimeType = inferUploadMimeType(fileName, info.mimeType)
      const chunks = []

      stream.on('data', (chunk) => {
        size += chunk.length
        chunks.push(chunk)
      })
      stream.on('limit', () => {
        reject(new Error(`file too large; max ${MAX_FILE_SIZE_BYTES} bytes`))
      })
      stream.on('end', () => {
        fileBuffer = Buffer.concat(chunks)
      })
      stream.on('error', reject)
    })

    busboy.on('error', reject)
    busboy.on('finish', () => {
      if (!gotFile || !fileBuffer) {
        reject(new Error('missing file'))
        return
      }
      resolve({
        fields,
        file: {
          buffer: fileBuffer,
          name: fileName,
          mimeType,
          size,
        },
      })
    })

    req.pipe(busboy)
  })
}

function uploadBufferToCos(input) {
  return new Promise((resolve, reject) => {
    if (!cos || !BUCKET || !REGION) {
      reject(new Error('COS server config missing; set COS_SECRET_ID, COS_SECRET_KEY, COS_REGION, COS_BUCKET'))
      return
    }

    cos.putObject(
      {
        Bucket: BUCKET,
        Region: REGION,
        Key: input.objectKey,
        Body: input.buffer,
        ContentLength: input.buffer.length,
        ContentType: input.mimeType,
        ...(shouldServeInline(input.mimeType) ? { ContentDisposition: 'inline' } : {}),
      },
      (error) => {
        if (error) {
          reject(error)
          return
        }
        resolve()
      },
    )
  })
}

async function handleUpload(req, res) {
  const parsed = await parseMultipart(req)
  const conversationId = String(parsed.fields.conversationId ?? '').trim()
  const postId = String(parsed.fields.postId ?? '').trim()
  if (!conversationId || !postId) {
    json(res, 400, { ok: false, error: 'conversationId and postId are required' })
    return
  }

  const objectKey = buildObjectKey({
    prefix: PREFIX,
    conversationId,
    postId,
    fileName: parsed.file.name,
    taskNo: parsed.fields.taskNo,
    sourceAgent: parsed.fields.sourceAgent || parsed.fields.providerId || parsed.fields.agentSource,
  })

  await uploadBufferToCos({
    objectKey,
    buffer: parsed.file.buffer,
    mimeType: parsed.file.mimeType,
  })

  json(res, 200, {
    ok: true,
    objectKey,
    objectUrl: publicObjectUrl(objectKey),
    name: parsed.file.name,
    size: parsed.file.size,
    mimeType: parsed.file.mimeType,
  })
}

async function handleRequest(req, res) {
  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Headers': 'Content-Type',
      'Access-Control-Allow-Methods': 'GET,POST,OPTIONS',
      Connection: 'close',
    })
    res.end()
    return
  }

  const url = new URL(req.url ?? '/', `http://${req.headers.host ?? `${HOST}:${PORT}`}`)

  if (req.method === 'GET' && url.pathname === '/health') {
    json(res, 200, {
      ok: true,
      hasCosConfig: Boolean(cos && BUCKET && REGION),
      bucket: BUCKET || null,
      region: REGION || null,
      prefix: PREFIX,
      maxFileSizeBytes: MAX_FILE_SIZE_BYTES,
    })
    return
  }

  if (req.method === 'POST' && url.pathname === '/upload') {
    await handleUpload(req, res)
    return
  }

  json(res, 404, { ok: false, error: 'not_found' })
}

const server = http.createServer((req, res) => {
  void handleRequest(req, res).catch((error) => {
    json(res, 500, {
      ok: false,
      error: error instanceof Error ? error.message : String(error),
    })
  })
})

server.listen(PORT, HOST, () => {
  console.log(`[cos-upload] http://${HOST}:${PORT} (bucket=${BUCKET || '-'} region=${REGION || '-'})`)
})
