import { createReadStream, existsSync } from 'node:fs'
import { readFile } from 'node:fs/promises'
import http from 'node:http'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { WebSocketServer } from 'ws'

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
  driveCommand: {
    x: 0,
    y: 0,
    left: 0,
    right: 0,
    mode: 'stop',
  },
}

const uiClients = new Set()
const deviceClients = new Set()

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

function createSnapshot() {
  return JSON.stringify({
    type: 'snapshot',
    ...state,
    deviceCount: deviceClients.size,
  })
}

function broadcastSnapshot() {
  const snapshot = createSnapshot()

  for (const client of uiClients) {
    if (client.readyState === client.OPEN) {
      client.send(snapshot)
    }
  }

  for (const client of deviceClients) {
    if (client.readyState === client.OPEN) {
      client.send(snapshot)
    }
  }
}

function registerClient(socket, role) {
  socket.role = role

  if (role === 'ui') {
    uiClients.add(socket)
  }

  if (role === 'device') {
    deviceClients.add(socket)
    state.lastSeenAt = new Date().toISOString()
  }

  socket.send(createSnapshot())
  broadcastSnapshot()
}

function unregisterClient(socket) {
  uiClients.delete(socket)
  deviceClients.delete(socket)
}

function relayLedCommand(nextState) {
  state.desiredState = nextState
  state.lastCommandAt = new Date().toISOString()
  broadcastSnapshot()
}

function relayDriveCommand(command) {
  state.driveCommand = command
  state.lastCommandAt = new Date().toISOString()
  broadcastSnapshot()
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
    sendJson(response, 200, {
      ...state,
      deviceCount: deviceClients.size,
    })
    return true
  }

  if (pathname === '/api/led' && request.method === 'POST') {
    const payload = await readJson(request)

    if (payload.state !== 'on' && payload.state !== 'off') {
      sendJson(response, 400, { error: 'state must be "on" or "off"' })
      return true
    }

    relayLedCommand(payload.state)
    sendJson(response, 200, {
      ...state,
      deviceCount: deviceClients.size,
    })
    return true
  }

  if (pathname === '/api/device/heartbeat' && request.method === 'POST') {
    const payload = await readJson(request)

    if (payload.state === 'on' || payload.state === 'off') {
      state.lastReportedState = payload.state
    }

    state.lastSeenAt = new Date().toISOString()
    broadcastSnapshot()
    sendJson(response, 200, {
      ...state,
      deviceCount: deviceClients.size,
    })
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

const websocketServer = new WebSocketServer({ noServer: true })

websocketServer.on('connection', (socket) => {
  socket.on('message', (rawMessage) => {
    try {
      const payload = JSON.parse(rawMessage.toString())

      if (payload.type === 'hello') {
        if (payload.role !== 'ui' && payload.role !== 'device') {
          socket.send(JSON.stringify({ type: 'error', message: 'invalid role' }))
          return
        }

        registerClient(socket, payload.role)
        return
      }

      if (payload.type === 'set-led') {
        if (socket.role !== 'ui') {
          socket.send(JSON.stringify({ type: 'error', message: 'ui only action' }))
          return
        }

        if (payload.state !== 'on' && payload.state !== 'off') {
          socket.send(JSON.stringify({ type: 'error', message: 'invalid state' }))
          return
        }

        relayLedCommand(payload.state)
        socket.send(JSON.stringify({ type: 'ack', state: payload.state }))
        return
      }

      if (payload.type === 'drive') {
        if (socket.role !== 'ui') {
          socket.send(JSON.stringify({ type: 'error', message: 'ui only action' }))
          return
        }

        const command = {
          x: Number(payload.x ?? 0),
          y: Number(payload.y ?? 0),
          left: Number(payload.left ?? 0),
          right: Number(payload.right ?? 0),
          mode: String(payload.mode ?? 'stop'),
        }

        relayDriveCommand(command)
        socket.send(JSON.stringify({ type: 'ack', command }))
        return
      }

      if (payload.type === 'device-state') {
        if (socket.role !== 'device') {
          socket.send(JSON.stringify({ type: 'error', message: 'device only action' }))
          return
        }

        if (payload.state === 'on' || payload.state === 'off') {
          state.lastReportedState = payload.state
        }

        state.lastSeenAt = new Date().toISOString()
        broadcastSnapshot()
      }
    } catch (error) {
      socket.send(JSON.stringify({ type: 'error', message: error.message }))
    }
  })

  socket.on('close', () => {
    unregisterClient(socket)
    broadcastSnapshot()
  })
})

server.on('upgrade', (request, socket, head) => {
  const url = new URL(request.url ?? '/', `http://${request.headers.host}`)

  if (url.pathname !== '/ws') {
    socket.destroy()
    return
  }

  websocketServer.handleUpgrade(request, socket, head, (websocket) => {
    websocketServer.emit('connection', websocket, request)
  })
})

server.listen(port, () => {
  console.log(`Railway server listening on port ${port}`)
})
