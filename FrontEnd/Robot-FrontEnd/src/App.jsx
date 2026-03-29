import { useEffect, useRef, useState } from 'react'
import './App.css'

const PAD_RADIUS = 110

const STOP_COMMAND = {
  x: 0,
  y: 0,
  left: 0,
  right: 0,
  mode: 'stop',
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value))
}

function roundDrive(value) {
  return Math.round(value * 100) / 100
}

function formatTimestamp(value) {
  if (!value) {
    return 'Waiting for first update'
  }

  return new Date(value).toLocaleString()
}

function normalizeVector(clientX, clientY, rect) {
  const centerX = rect.left + rect.width / 2
  const centerY = rect.top + rect.height / 2

  const rawX = (clientX - centerX) / (rect.width / 2)
  const rawY = (centerY - clientY) / (rect.height / 2)

  const distance = Math.hypot(rawX, rawY)
  if (distance <= 1) {
    return { x: rawX, y: rawY }
  }

  return {
    x: rawX / distance,
    y: rawY / distance,
  }
}

function createDriveCommand(x, y) {
  const normalizedX = roundDrive(clamp(x, -1, 1))
  const normalizedY = roundDrive(clamp(y, -1, 1))
  const left = roundDrive(clamp(normalizedY + normalizedX, -1, 1))
  const right = roundDrive(clamp(normalizedY - normalizedX, -1, 1))

  let mode = 'stop'
  if (Math.abs(normalizedX) < 0.08 && Math.abs(normalizedY) < 0.08) {
    mode = 'stop'
  } else if (Math.abs(normalizedY) >= Math.abs(normalizedX)) {
    mode = normalizedY > 0 ? 'forward' : 'reverse'
  } else {
    mode = normalizedX > 0 ? 'right' : 'left'
  }

  return {
    x: normalizedX,
    y: normalizedY,
    left,
    right,
    mode,
  }
}

function App() {
  const [lastSeenAt, setLastSeenAt] = useState(null)
  const [lastCommandAt, setLastCommandAt] = useState(null)
  const [deviceCount, setDeviceCount] = useState(0)
  const [error, setError] = useState('')
  const [connected, setConnected] = useState(false)
  const [driveCommand, setDriveCommand] = useState(STOP_COMMAND)
  const socketRef = useRef(null)
  const padRef = useRef(null)
  const pointerIdRef = useRef(null)

  function applySnapshot(payload) {
    setLastSeenAt(payload.lastSeenAt ?? null)
    setLastCommandAt(payload.lastCommandAt ?? null)
    setDeviceCount(payload.deviceCount ?? 0)
    setDriveCommand(payload.driveCommand ?? STOP_COMMAND)
  }

  useEffect(() => {
    let reconnectTimerId = 0
    let stopped = false

    function connect() {
      const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
      const socket = new WebSocket(`${protocol}//${window.location.host}/ws`)
      socketRef.current = socket

      socket.addEventListener('open', () => {
        setConnected(true)
        setError('')
        socket.send(JSON.stringify({ type: 'hello', role: 'ui' }))
      })

      socket.addEventListener('message', (event) => {
        try {
          const payload = JSON.parse(event.data)

          if (payload.type === 'snapshot') {
            applySnapshot(payload)
          }

          if (payload.type === 'error') {
            setError(payload.message ?? 'WebSocket error')
          }
        } catch (parseError) {
          setError(parseError.message)
        }
      })

      socket.addEventListener('close', () => {
        setConnected(false)
        socketRef.current = null

        if (!stopped) {
          reconnectTimerId = window.setTimeout(connect, 1000)
        }
      })

      socket.addEventListener('error', () => {
        setError('Realtime connection failed')
      })
    }

    connect()

    return () => {
      stopped = true
      window.clearTimeout(reconnectTimerId)
      socketRef.current?.close()
    }
  }, [])

  function sendDriveCommand(command) {
    setDriveCommand(command)

    if (!socketRef.current || socketRef.current.readyState !== WebSocket.OPEN) {
      setError('WebSocket is not connected')
      return
    }

    socketRef.current.send(
      JSON.stringify({
        type: 'drive',
        ...command,
      }),
    )
  }

  function updateFromPointer(event) {
    if (!padRef.current) {
      return
    }

    const vector = normalizeVector(
      event.clientX,
      event.clientY,
      padRef.current.getBoundingClientRect(),
    )

    sendDriveCommand(createDriveCommand(vector.x, vector.y))
  }

  function handlePointerDown(event) {
    pointerIdRef.current = event.pointerId
    event.currentTarget.setPointerCapture(event.pointerId)
    updateFromPointer(event)
  }

  function handlePointerMove(event) {
    if (pointerIdRef.current !== event.pointerId) {
      return
    }

    updateFromPointer(event)
  }

  function releasePad(event) {
    if (pointerIdRef.current !== event.pointerId) {
      return
    }

    pointerIdRef.current = null
    event.currentTarget.releasePointerCapture(event.pointerId)
    sendDriveCommand(STOP_COMMAND)
  }

  const knobStyle = {
    transform: `translate(calc(-50% + ${driveCommand.x * PAD_RADIUS}px), calc(-50% + ${-driveCommand.y * PAD_RADIUS}px))`,
  }

  return (
    <main className="shell">
      <section className="panel hero">
        <p className="eyebrow">Railway Control Surface</p>
        <h1>Drive Pad Controller</h1>
        <p className="lede">
          Drag inside the circle to set steering and throttle. The browser sends
          normalized left and right motor values over WebSocket immediately.
        </p>
      </section>

      <section className="controller-grid">
        <article className="panel controller-panel">
          <div
            ref={padRef}
            className="drive-pad"
            onPointerDown={handlePointerDown}
            onPointerMove={handlePointerMove}
            onPointerUp={releasePad}
            onPointerCancel={releasePad}
          >
            <div className="drive-ring ring-outer" />
            <div className="drive-ring ring-mid" />
            <div className="drive-ring ring-inner" />
            <div className="crosshair crosshair-h" />
            <div className="crosshair crosshair-v" />
            <div className="drive-knob" style={knobStyle} />
          </div>

          <div className="quick-controls">
            <button type="button" onPointerDown={() => sendDriveCommand(createDriveCommand(0, 1))} onPointerUp={() => sendDriveCommand(STOP_COMMAND)} onPointerLeave={() => sendDriveCommand(STOP_COMMAND)}>
              Forward
            </button>
            <button type="button" onPointerDown={() => sendDriveCommand(createDriveCommand(-0.8, 0))} onPointerUp={() => sendDriveCommand(STOP_COMMAND)} onPointerLeave={() => sendDriveCommand(STOP_COMMAND)}>
              Left
            </button>
            <button type="button" className="stop" onClick={() => sendDriveCommand(STOP_COMMAND)}>
              Stop
            </button>
            <button type="button" onPointerDown={() => sendDriveCommand(createDriveCommand(0.8, 0))} onPointerUp={() => sendDriveCommand(STOP_COMMAND)} onPointerLeave={() => sendDriveCommand(STOP_COMMAND)}>
              Right
            </button>
            <button type="button" onPointerDown={() => sendDriveCommand(createDriveCommand(0, -1))} onPointerUp={() => sendDriveCommand(STOP_COMMAND)} onPointerLeave={() => sendDriveCommand(STOP_COMMAND)}>
              Reverse
            </button>
          </div>
        </article>

        <section className="status-grid">
          <article className="panel status-card">
            <span className="label">Drive Mode</span>
            <strong className={`value state ${driveCommand.mode}`}>
              {driveCommand.mode}
            </strong>
            <span className="meta">Last command: {formatTimestamp(lastCommandAt)}</span>
          </article>

          <article className="panel status-card">
            <span className="label">Motor Mix</span>
            <strong className="value">
              L {driveCommand.left} / R {driveCommand.right}
            </strong>
            <span className="meta">X {driveCommand.x} / Y {driveCommand.y}</span>
          </article>

          <article className="panel status-card">
            <span className="label">Connection</span>
            <strong className="value">
              {error ? 'Degraded' : connected ? 'Realtime' : 'Connecting'}
            </strong>
            <span className="meta">
              {error || `${deviceCount} device client${deviceCount === 1 ? '' : 's'} connected`}
            </span>
          </article>

          <article className="panel status-card">
            <span className="label">Robot Check-In</span>
            <strong className="value">Telemetry</strong>
            <span className="meta">Last seen: {formatTimestamp(lastSeenAt)}</span>
          </article>
        </section>
      </section>
    </main>
  )
}

export default App
