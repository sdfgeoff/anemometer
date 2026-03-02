import { useEffect, useMemo, useState, type FormEvent } from 'react'
import './App.css'
import {
  createWindDataProvider,
  type CurrentResponse,
  type WindPoint,
} from './dataProvider'

const MAX_POINTS_24H = 24 * 60 * 2
const MAX_POINTS_WEEK = 7 * 24 * 60 * 2

type WifiStatus = {
  ok: boolean
  ap: {
    active: boolean
    ssid: string
    ip: string
  }
  sta: {
    status: number
    connected: boolean
    ssid: string
    ip: string
  }
  credentialsSaved: boolean
}

function formatMps(v?: number): string {
  if (v === undefined) return '--'
  return `${v.toFixed(2)} m/s`
}

function speedToKmh(mps: number): number {
  return mps * 3.6
}

function speedToKnots(mps: number): number {
  return mps * 1.943844
}

function TinyChart({ points }: { points: WindPoint[] }) {
  const { path, gridY } = useMemo(() => {
    if (points.length < 2) {
      return { path: '', gridY: [52, 104, 156, 208] }
    }

    const width = 1000
    const height = 260
    const maxY = Math.max(1, ...points.map((p) => p.mps))
    const minY = Math.min(0, ...points.map((p) => p.mps))
    const spanY = Math.max(0.001, maxY - minY)
    const lines = [0.2, 0.4, 0.6, 0.8].map((f) => height * f)

    const d = points
      .map((p, i) => {
        const x = (i / (points.length - 1)) * width
        const y = height - ((p.mps - minY) / spanY) * height
        return `${i === 0 ? 'M' : 'L'}${x.toFixed(2)},${y.toFixed(2)}`
      })
      .join(' ')

    return { path: d, gridY: lines }
  }, [points])

  return (
    <svg className="chart" viewBox="0 0 1000 260" preserveAspectRatio="none">
      <defs>
        <linearGradient id="lineGradient" x1="0" x2="0" y1="0" y2="1">
          <stop offset="0%" stopColor="#2a9d8f" />
          <stop offset="100%" stopColor="#264653" />
        </linearGradient>
      </defs>
      <rect x="0" y="0" width="1000" height="260" fill="#f2efe7" />
      {gridY.map((y) => (
        <line key={y} className="chartGrid" x1="0" y1={y} x2="1000" y2={y} />
      ))}
      <path d={path} fill="none" stroke="url(#lineGradient)" strokeWidth="4" />
    </svg>
  )
}

function App() {
  const [current, setCurrent] = useState<CurrentResponse | null>(null)
  const [dayPoints, setDayPoints] = useState<WindPoint[]>([])
  const [weekPoints, setWeekPoints] = useState<WindPoint[]>([])
  const [selectedRange, setSelectedRange] = useState<'24h' | 'week'>('24h')
  const [error, setError] = useState<string | null>(null)
  const [wifiStatus, setWifiStatus] = useState<WifiStatus | null>(null)
  const [wifiSsid, setWifiSsid] = useState('')
  const [wifiPassword, setWifiPassword] = useState('')
  const [wifiMessage, setWifiMessage] = useState<string | null>(null)
  const [showSettings, setShowSettings] = useState(false)

  useEffect(() => {
    let cancelled = false
    const provider = createWindDataProvider()

    const loadCurrent = async () => {
      try {
        const data = await provider.getCurrent()
        if (!cancelled) {
          setCurrent(data)
          setError(null)
        }
      } catch (e) {
        if (!cancelled) setError((e as Error).message)
      }
    }

    const loadHistories = async () => {
      try {
        const [day, week] = await Promise.all([
          provider.getHistory('24h'),
          provider.getHistory('week'),
        ])
        if (!cancelled) {
          setDayPoints(day.points.slice(-MAX_POINTS_24H))
          setWeekPoints(week.points.slice(-MAX_POINTS_WEEK))
          setError(null)
        }
      } catch (e) {
        if (!cancelled) setError((e as Error).message)
      }
    }

    const loadWifiStatus = async () => {
      try {
        const res = await fetch('/api/wifi/status')
        if (!res.ok) {
          throw new Error(`HTTP ${res.status}`)
        }
        const data = (await res.json()) as WifiStatus
        if (!cancelled) {
          setWifiStatus(data)
          setWifiSsid((prev) => (prev.length === 0 ? data.sta.ssid ?? '' : prev))
        }
      } catch {
        if (!cancelled) {
          setWifiStatus(null)
        }
      }
    }

    loadCurrent()
    loadHistories()
    loadWifiStatus()

    const currentTimer = window.setInterval(loadCurrent, 5000)
    const historyTimer = window.setInterval(loadHistories, 30000)
    const wifiTimer = window.setInterval(loadWifiStatus, 5000)

    return () => {
      cancelled = true
      window.clearInterval(currentTimer)
      window.clearInterval(historyTimer)
      window.clearInterval(wifiTimer)
    }
  }, [])

  const selectedPoints = selectedRange === '24h' ? dayPoints : weekPoints
  const latestMps = current?.mps ?? selectedPoints.at(-1)?.mps

  const onWifiSubmit = async (e: FormEvent) => {
    e.preventDefault()
    setWifiMessage('Saving Wi-Fi credentials and connecting...')

    const body = new URLSearchParams()
    body.set('ssid', wifiSsid)
    body.set('password', wifiPassword)

    try {
      const res = await fetch('/api/wifi/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body,
      })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      setWifiMessage('Saved. The anemometer is now attempting STA connection.')
    } catch (err) {
      setWifiMessage(`Failed to save Wi-Fi config: ${(err as Error).message}`)
    }
  }

  const onWifiClear = async () => {
    setWifiMessage('Clearing Wi-Fi credentials...')
    try {
      const res = await fetch('/api/wifi/clear', { method: 'POST' })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      setWifiPassword('')
      setWifiMessage('Saved credentials cleared.')
    } catch (err) {
      setWifiMessage(`Failed to clear Wi-Fi config: ${(err as Error).message}`)
    }
  }

  return (
    <main className="page">
      <section className="topBar">
        <button type="button" className="settingsButton" onClick={() => setShowSettings((v) => !v)}>
          Settings
        </button>
      </section>

      {showSettings && (
        <section className="panel settingsPanel">
          <p className="meta">
            AP: {wifiStatus?.ap.active ? `on (${wifiStatus.ap.ssid} / ${wifiStatus.ap.ip})` : 'off'} | STA:{' '}
            {wifiStatus?.sta.connected
              ? `connected (${wifiStatus.sta.ssid} / ${wifiStatus.sta.ip})`
              : 'not connected'}
          </p>
          <form className="wifiForm" onSubmit={onWifiSubmit}>
            <input
              value={wifiSsid}
              onChange={(e) => setWifiSsid(e.target.value)}
              placeholder="Wi-Fi SSID"
              required
            />
            <input
              value={wifiPassword}
              onChange={(e) => setWifiPassword(e.target.value)}
              placeholder="Wi-Fi Password"
              type="password"
            />
            <div className="buttons">
              <button type="submit">Save and Connect</button>
              <button type="button" onClick={onWifiClear}>
                Clear Saved
              </button>
            </div>
          </form>
          {wifiMessage && <p className="meta">{wifiMessage}</p>}
        </section>
      )}

      <section className="panel hero">
        <p className="eyebrow">Anemometer</p>
        <h1>Live Wind Monitor</h1>
        <div className="stats">
          <article>
            <h2>Current</h2>
            <p>{formatMps(latestMps)}</p>
          </article>
          <article>
            <h2>Current (km/h)</h2>
            <p>{latestMps !== undefined ? `${speedToKmh(latestMps).toFixed(2)} km/h` : '--'}</p>
          </article>
          <article>
            <h2>Current (kn)</h2>
            <p>{latestMps !== undefined ? `${speedToKnots(latestMps).toFixed(2)} kn` : '--'}</p>
          </article>
          <article>
            <h2>Source</h2>
            <p>{current?.source ?? 'n/a'}</p>
          </article>
        </div>
      </section>

      <section className="panel">
        <header className="chartHeader">
          <h2>History</h2>
          <div className="buttons">
            <button
              className={selectedRange === '24h' ? 'active' : ''}
              onClick={() => setSelectedRange('24h')}
            >
              Last 24h
            </button>
            <button
              className={selectedRange === 'week' ? 'active' : ''}
              onClick={() => setSelectedRange('week')}
            >
              Last 7d
            </button>
          </div>
        </header>
        <TinyChart points={selectedPoints} />
        <p className="meta">
          {selectedPoints.length} points | sample every 30s | auto-refresh without page reload
        </p>
        {error && <p className="error">{error}</p>}
      </section>
    </main>
  )
}

export default App
