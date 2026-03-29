import { useEffect, useState } from 'react'
import './App.css'

const POLL_INTERVAL_MS = 3000

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
  const [loading, setLoading] = useState(true)
  const [mutating, setMutating] = useState(false)
  const [error, setError] = useState('')

  async function fetchStatus() {
    try {
      const response = await fetch('/api/led')

      if (!response.ok) {
        throw new Error(`Status request failed with ${response.status}`)
      }

      const payload = await response.json()
      setDesiredState(payload.desiredState ?? 'off')
      setReportedState(payload.lastReportedState ?? 'unknown')
      setLastSeenAt(payload.lastSeenAt ?? null)
      setLastCommandAt(payload.lastCommandAt ?? null)
      setError('')
    } catch (requestError) {
      setError(requestError.message)
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => {
    fetchStatus()

    const intervalId = window.setInterval(fetchStatus, POLL_INTERVAL_MS)

    return () => window.clearInterval(intervalId)
  }, [])

  async function updateState(nextState) {
    setMutating(true)

    try {
      const response = await fetch('/api/led', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({ state: nextState }),
      })

      if (!response.ok) {
        throw new Error(`Update failed with ${response.status}`)
      }

      const payload = await response.json()
      setDesiredState(payload.desiredState ?? nextState)
      setLastCommandAt(payload.lastCommandAt ?? new Date().toISOString())
      setError('')
    } catch (requestError) {
      setError(requestError.message)
    } finally {
      setMutating(false)
    }
  }

  return (
    <main className="shell">
      <section className="panel hero">
        <p className="eyebrow">Railway Control Surface</p>
        <h1>ESP32 LED Command Center</h1>
        <p className="lede">
          This site stores the latest LED command in Railway. Your ESP32 reads it
          over home Wi-Fi and applies the change on its next poll.
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
          The ESP32 does not need a browser open. It only needs internet access
          to your Railway app.
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
            {loading ? 'Loading' : error ? 'Degraded' : 'Online'}
          </strong>
          <span className="meta">
            {error || 'UI polling Railway every 3 seconds'}
          </span>
        </article>
      </section>
    </main>
  )
}

export default App
