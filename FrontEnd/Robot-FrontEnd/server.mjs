import { createReadStream, existsSync } from 'node:fs'
import { readFile } from 'node:fs/promises'
import http from 'node:http'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)
const distDir = path.join(__dirname, 'dist')
const indexPath = path.join(distDir, 'index.html')
const port = Number.parseInt(process.env.PORT ?? '3000', 10)

const state = {
  desiredState: 'off',
  lastCommandAt: null,
  lastReportedState: 'unknown',
  lastSeenAt: null,
}

const contentTypes = {
  '.css': 'text/css; charset=utf-8',
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
}

function sendJson(response, statusCode, payload) {
  response.writeHead(statusCode, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
  })
  response.end(JSON.stringify(payload))
}

async function readJson(request) {
  const chunks = []

  for await (const chunk of request) {
    chunks.push(chunk)
  }

  const body = Buffer.concat(chunks).toString('utf8')
  return body ? JSON.parse(body) : {}
}

function serveFile(response, filePath) {
  const extension = path.extname(filePath)
  const contentType = contentTypes[extension] ?? 'application/octet-stream'

  response.writeHead(200, { 'Content-Type': contentType })
  createReadStream(filePath).pipe(response)
}

async function handleApi(request, response, pathname) {
  if (pathname === '/api/led' && request.method === 'GET') {
    sendJson(response, 200, state)
    return true
  }

  if (pathname === '/api/led' && request.method === 'POST') {
    const payload = await readJson(request)

    if (payload.state !== 'on' && payload.state !== 'off') {
      sendJson(response, 400, { error: 'state must be "on" or "off"' })
      return true
    }

    state.desiredState = payload.state
    state.lastCommandAt = new Date().toISOString()
    sendJson(response, 200, state)
    return true
  }

  if (pathname === '/api/device/heartbeat' && request.method === 'POST') {
    const payload = await readJson(request)

    if (payload.state === 'on' || payload.state === 'off') {
      state.lastReportedState = payload.state
    }

    state.lastSeenAt = new Date().toISOString()
    sendJson(response, 200, state)
    return true
  }

  return false
}

const server = http.createServer(async (request, response) => {
  try {
    const url = new URL(request.url ?? '/', `http://${request.headers.host}`)
    const pathname = url.pathname

    if (await handleApi(request, response, pathname)) {
      return
    }

    const requestedPath = pathname === '/' ? indexPath : path.join(distDir, pathname)

    if (existsSync(requestedPath) && !requestedPath.endsWith(path.sep)) {
      serveFile(response, requestedPath)
      return
    }

    if (existsSync(indexPath)) {
      response.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' })
      response.end(await readFile(indexPath, 'utf8'))
      return
    }

    sendJson(response, 503, {
      error: 'Frontend build not found. Run "npm run build" before starting the server.',
    })
  } catch (error) {
    sendJson(response, 500, { error: error.message })
  }
})

server.listen(port, () => {
  console.log(`Railway server listening on port ${port}`)
})
