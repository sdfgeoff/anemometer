import { useEffect, useMemo, useState } from 'react'
import './App.css'
import {
  createWindDataProvider,
  type CurrentResponse,
  type WindPoint,
} from './dataProvider'

const MAX_POINTS_24H = 24 * 60 * 2
const MAX_POINTS_WEEK = 7 * 24 * 60 * 2

function formatMps(v?: number): string {
  if (v === undefined) return '--'
  return `${v.toFixed(2)} m/s`
}

function speedToKmh(mps: number): number {
  return mps * 3.6
}

function TinyChart({ points }: { points: WindPoint[] }) {
  const path = useMemo(() => {
    if (points.length < 2) return ''

    const width = 1000
    const height = 260
    const maxY = Math.max(1, ...points.map((p) => p.mps))
    const minY = Math.min(0, ...points.map((p) => p.mps))
    const spanY = Math.max(0.001, maxY - minY)

    return points
      .map((p, i) => {
        const x = (i / (points.length - 1)) * width
        const y = height - ((p.mps - minY) / spanY) * height
        return `${i === 0 ? 'M' : 'L'}${x.toFixed(2)},${y.toFixed(2)}`
      })
      .join(' ')
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

    loadCurrent()
    loadHistories()

    const currentTimer = window.setInterval(loadCurrent, 5000)
    const historyTimer = window.setInterval(loadHistories, 30000)

    return () => {
      cancelled = true
      window.clearInterval(currentTimer)
      window.clearInterval(historyTimer)
    }
  }, [])

  const selectedPoints = selectedRange === '24h' ? dayPoints : weekPoints
  const latestMps = current?.mps ?? selectedPoints.at(-1)?.mps

  return (
    <main className="page">
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
