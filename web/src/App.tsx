import { useEffect, useMemo, useState, type FormEvent } from 'react'
import './App.css'
import {
  createWindDataProvider,
  type CurrentResponse,
  type WindPoint,
} from './dataProvider'

const MAX_POINTS_24H = 24 * 60 * 2
const MAX_POINTS_WEEK = 7 * 24 * 60 * 2
const SAMPLE_INTERVAL_SECONDS = 30

type DisplayUnit = 'mps' | 'kmh' | 'kn'

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

function unitLabel(unit: DisplayUnit): string {
  if (unit === 'mps') return 'm/s'
  if (unit === 'kmh') return 'km/h'
  return 'kn'
}

function convertSpeedFromMps(mps: number, unit: DisplayUnit): number {
  if (unit === 'mps') return mps
  if (unit === 'kmh') return mps * 3.6
  return mps * 1.943844
}

function formatSpeed(mps: number | undefined, unit: DisplayUnit): string {
  if (mps === undefined) return '--'
  return `${convertSpeedFromMps(mps, unit).toFixed(2)} ${unitLabel(unit)}`
}

function formatTs(tsMs: number): string {
  const d = new Date(tsMs)
  const mm = `${d.getMonth() + 1}`
  const dd = `${d.getDate()}`
  const hh = `${d.getHours()}`.padStart(2, '0')
  const mi = `${d.getMinutes()}`.padStart(2, '0')
  return `${mm}/${dd} ${hh}:${mi}`
}

function TinyChart({
  points,
  unit,
  nowMs,
}: {
  points: WindPoint[]
  unit: DisplayUnit
  nowMs: number
}) {
  const { path, yGrid, xTicks } = useMemo(() => {
    const width = 1000
    const height = 300
    const padLeft = 58
    const padRight = 16
    const padTop = 10
    const padBottom = 34
    const plotW = width - padLeft - padRight
    const plotH = height - padTop - padBottom

    if (points.length < 2) {
      const emptyY = [0, 1, 2, 3, 4].map((i) => ({
        y: padTop + (i / 4) * plotH,
        value: 0,
      }))
      return { path: '', yGrid: emptyY, xTicks: [] as Array<{ x: number; label: string }> }
    }

    const displayValues = points.map((p) => convertSpeedFromMps(p.mps, unit))
    const maxY = Math.max(1, ...displayValues)
    const minY = Math.min(0, ...displayValues)
    const spanY = Math.max(0.001, maxY - minY)

    const d = displayValues
      .map((v, i) => {
        const x = padLeft + (i / (displayValues.length - 1)) * plotW
        const y = padTop + (1 - (v - minY) / spanY) * plotH
        return `${i === 0 ? 'M' : 'L'}${x.toFixed(2)},${y.toFixed(2)}`
      })
      .join(' ')

    const grid = [0, 1, 2, 3, 4].map((i) => {
      const frac = i / 4
      return {
        y: padTop + frac * plotH,
        value: maxY - frac * spanY,
      }
    })

    // Use browser-local clock since ESP has no RTC; map sample index to now.
    const startMs = nowMs - (points.length - 1) * SAMPLE_INTERVAL_SECONDS * 1000
    const ticks = [0, 1, 2, 3, 4].map((i) => {
      const frac = i / 4
      const x = padLeft + frac * plotW
      const labelMs = startMs + frac * (nowMs - startMs)
      return { x, label: formatTs(labelMs) }
    })

    return { path: d, yGrid: grid, xTicks: ticks }
  }, [points, unit, nowMs])

  return (
    <svg className="chart" viewBox="0 0 1000 300" preserveAspectRatio="none">
      <defs>
        <linearGradient id="lineGradient" x1="0" x2="0" y1="0" y2="1">
          <stop offset="0%" stopColor="#2a9d8f" />
          <stop offset="100%" stopColor="#264653" />
        </linearGradient>
      </defs>
      <rect x="0" y="0" width="1000" height="300" fill="#f2efe7" />
      <line className="chartAxis" x1="58" y1="10" x2="58" y2="266" />
      <line className="chartAxis" x1="58" y1="266" x2="984" y2="266" />
      {yGrid.map((g) => (
        <g key={`y-${g.y}`}>
          <line className="chartGrid" x1="58" y1={g.y} x2="984" y2={g.y} />
          <text className="chartAxisLabel" x="52" y={g.y + 4} textAnchor="end">
            {g.value.toFixed(1)}
          </text>
        </g>
      ))}
      {xTicks.map((t) => (
        <g key={`x-${t.x}`}>
          <line className="chartTick" x1={t.x} y1="266" x2={t.x} y2="272" />
          <text className="chartAxisLabel" x={t.x} y="288" textAnchor="middle">
            {t.label}
          </text>
        </g>
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
  const [selectedUnit, setSelectedUnit] = useState<DisplayUnit>('kmh')
  const [browserNowMs, setBrowserNowMs] = useState(() => Date.now())
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

  useEffect(() => {
    const timer = window.setInterval(() => setBrowserNowMs(Date.now()), 30000)
    return () => window.clearInterval(timer)
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
      <section className="panel hero">
        <p className="eyebrow">Anemometer</p>
        <div className="heroHeader">
          <h1>Live Wind Monitor</h1>
          <button
            type="button"
            className="settingsButton"
            onClick={() => setShowSettings((v) => !v)}
          >
            Settings
          </button>
        </div>
        <div className="heroControls">
          <label htmlFor="unitSelect">Units</label>
          <select
            id="unitSelect"
            value={selectedUnit}
            onChange={(e) => setSelectedUnit(e.target.value as DisplayUnit)}
          >
            <option value="kmh">km/h</option>
            <option value="mps">m/s</option>
            <option value="kn">knots</option>
          </select>
        </div>
        <div className="stats">
          <article>
            <h2>Current Speed</h2>
            <p>{formatSpeed(latestMps, selectedUnit)}</p>
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
        <TinyChart points={selectedPoints} unit={selectedUnit} nowMs={browserNowMs} />
        <p className="meta">
          {selectedPoints.length} points | sample every 30s | axis times use browser local clock
        </p>
        {error && <p className="error">{error}</p>}
      </section>

      {showSettings && (
        <section className="settingsModalWrap" onClick={() => setShowSettings(false)}>
          <article className="panel settingsModal" onClick={(e) => e.stopPropagation()}>
            <header className="chartHeader">
              <h2>Device Settings</h2>
              <button type="button" onClick={() => setShowSettings(false)}>
                Close
              </button>
            </header>
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
          </article>
        </section>
      )}
    </main>
  )
}

export default App
