import { useEffect, useRef, useState } from 'react'
import './App.css'

function formatTimestamp(value) {
  if (!value) {
    return 'Waiting for first update'
  }

  return new Date(value).toLocaleString()
}

function App() {
  const [desiredState, setDesiredState] = useState('off')
  const [reportedState, setReportedState] = useState('unknown')
  const [lastSeenAt, setLastSeenAt] = useState(null)
  const [lastCommandAt, setLastCommandAt] = useState(null)
  const [deviceCount, setDeviceCount] = useState(0)
  const [mutating, setMutating] = useState(false)
  const [error, setError] = useState('')
  const [connected, setConnected] = useState(false)
  const socketRef = useRef(null)

  function applySnapshot(payload) {
    setDesiredState(payload.desiredState ?? 'off')
    setReportedState(payload.lastReportedState ?? 'unknown')
    setLastSeenAt(payload.lastSeenAt ?? null)
    setLastCommandAt(payload.lastCommandAt ?? null)
    setDeviceCount(payload.deviceCount ?? 0)
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
            setMutating(false)
          }

          if (payload.type === 'ack') {
            setMutating(false)
          }

          if (payload.type === 'error') {
            setError(payload.message ?? 'WebSocket error')
            setMutating(false)
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

  function updateState(nextState) {
    if (!socketRef.current || socketRef.current.readyState !== WebSocket.OPEN) {
      setError('WebSocket is not connected')
      return
    }

    setMutating(true)
    setDesiredState(nextState)
    socketRef.current.send(JSON.stringify({ type: 'set-led', state: nextState }))
  }

  return (
    <main className="shell">
      <section className="panel hero">
        <p className="eyebrow">Railway Control Surface</p>
        <h1>ESP32 LED Command Center</h1>
        <p className="lede">
          Railway keeps a live WebSocket open to both the browser and the ESP32,
          so LED commands are forwarded immediately instead of waiting for a poll.
        </p>

        <div className="actions">
          <button
            className="action on"
            onClick={() => updateState('on')}
            disabled={mutating}
            type="button"
          >
            Turn LED On
          </button>
          <button
            className="action off"
            onClick={() => updateState('off')}
            disabled={mutating}
            type="button"
          >
            Turn LED Off
          </button>
        </div>

        <p className="hint">
          The ESP32 only needs outbound internet access to your Railway app.
        </p>
      </section>

      <section className="status-grid">
        <article className="panel status-card">
          <span className="label">Requested State</span>
          <strong className={`value state ${desiredState}`}>{desiredState}</strong>
          <span className="meta">Last command: {formatTimestamp(lastCommandAt)}</span>
        </article>

        <article className="panel status-card">
          <span className="label">Device Report</span>
          <strong className={`value state ${reportedState}`}>{reportedState}</strong>
          <span className="meta">Last seen: {formatTimestamp(lastSeenAt)}</span>
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
      </section>
    </main>
  )
}

export default App
