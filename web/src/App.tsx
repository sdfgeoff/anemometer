import { useEffect, useMemo, useState, type FormEvent } from 'react'
import './App.css'
import {
  createWindDataProvider,
  type CurrentResponse,
  type WindPoint,
} from './dataProvider'

type DisplayUnit = 'mps' | 'kmh' | 'kn'
type RangeKey = '15m' | '30m' | '1h' | '4h' | '12h' | '24h' | '3d' | 'week'

const RANGE_OPTIONS: Array<{ key: RangeKey; label: string; seconds: number }> = [
  { key: '15m', label: 'Last 15 min', seconds: 15 * 60 },
  { key: '30m', label: 'Last 30 min', seconds: 30 * 60 },
  { key: '1h', label: 'Last 1 hour', seconds: 60 * 60 },
  { key: '4h', label: 'Last 4 hours', seconds: 4 * 60 * 60 },
  { key: '12h', label: 'Last 12 hours', seconds: 12 * 60 * 60 },
  { key: '24h', label: 'Last 24 hours', seconds: 24 * 60 * 60 },
  { key: '3d', label: 'Last 3 days', seconds: 3 * 24 * 60 * 60 },
  { key: 'week', label: 'Last week', seconds: 7 * 24 * 60 * 60 },
]

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

type SensorStatus = {
  ok: boolean
  mode: 'dummy' | 'rpr220'
  threshold?: number
  baseline?: number
  reflected?: number
  signal?: number
  aboveThreshold?: boolean
  calibrating?: boolean
  calibrationMin?: number
  calibrationMax?: number
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

function formatVoltage(volts: number | undefined): string {
  if (volts === undefined) return '--'
  return `${volts.toFixed(2)} V`
}

function formatTs(tsMs: number): string {
  const d = new Date(tsMs)
  const mm = `${d.getMonth() + 1}`
  const dd = `${d.getDate()}`
  const hh = `${d.getHours()}`.padStart(2, '0')
  const mi = `${d.getMinutes()}`.padStart(2, '0')
  return `${mm}/${dd} ${hh}:${mi}`
}

function buildNiceYAxis(maxValue: number): { top: number; step: number; tickCount: number } {
  const safeMax = Math.max(0.1, maxValue)
  const targetTicks = 6
  const roughStep = safeMax / targetTicks
  const power = Math.pow(10, Math.floor(Math.log10(roughStep)))
  const multipliers = [1, 1.5, 2, 2.5, 5, 10]

  let step = multipliers[multipliers.length - 1] * power
  for (const m of multipliers) {
    const candidate = m * power
    if (candidate >= roughStep) {
      step = candidate
      break
    }
  }

  let top = Math.ceil(safeMax / step) * step
  if (top < step * 4) {
    top = step * 4
  }

  let tickCount = Math.round(top / step)
  if (tickCount > 10) {
    const factor = Math.ceil(tickCount / 8)
    step *= factor
    top = Math.ceil(safeMax / step) * step
    tickCount = Math.round(top / step)
  }

  return { top, step, tickCount }
}

function buildNiceAxisRange(
  minValue: number,
  maxValue: number,
  includeZero: boolean
): { min: number; max: number; step: number; tickCount: number } {
  let min = minValue
  let max = maxValue
  if (includeZero) {
    min = Math.min(0, min)
    max = Math.max(0, max)
  }
  if (!Number.isFinite(min) || !Number.isFinite(max) || min === max) {
    min = 0
    max = 1
  }

  const span = Math.max(0.1, max - min)
  const targetTicks = 6
  const roughStep = span / targetTicks
  const power = Math.pow(10, Math.floor(Math.log10(roughStep)))
  const multipliers = [1, 1.5, 2, 2.5, 5, 10]

  let step = multipliers[multipliers.length - 1] * power
  for (const m of multipliers) {
    const candidate = m * power
    if (candidate >= roughStep) {
      step = candidate
      break
    }
  }

  const axisMin = Math.floor(min / step) * step
  const axisMax = Math.ceil(max / step) * step
  let tickCount = Math.round((axisMax - axisMin) / step)
  if (tickCount < 2) tickCount = 2

  return { min: axisMin, max: axisMax, step, tickCount }
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
  const { windPath, batteryPath, solarPath, yGrid, yRightTicks, xTicks } = useMemo(() => {
    const width = 1000
    const height = 300
    const padLeft = 58
    const padRight = 74
    const padTop = 10
    const padBottom = 34
    const plotW = width - padLeft - padRight
    const plotH = height - padTop - padBottom

    if (points.length < 2) {
      const axis = buildNiceYAxis(5) // left (wind)
      const voltageAxis = buildNiceAxisRange(0, 20, true) // right (voltage)
      const emptyY = Array.from({ length: axis.tickCount + 1 }, (_, i) => {
        const value = axis.top - i * axis.step
        return { y: padTop + (i / axis.tickCount) * plotH, value }
      })
      const rightTicks = Array.from({ length: voltageAxis.tickCount + 1 }, (_, i) => {
        const frac = i / voltageAxis.tickCount
        return {
          y: padTop + frac * plotH,
          value: voltageAxis.max - frac * (voltageAxis.max - voltageAxis.min),
        }
      })
      return {
        windPath: '',
        batteryPath: '',
        solarPath: '',
        yGrid: emptyY,
        yRightTicks: rightTicks,
        xTicks: [] as Array<{ x: number; label: string }>,
      }
    }

    const displayValues = points.map((p) => convertSpeedFromMps(p.mps, unit))
    const windAxis = buildNiceYAxis(Math.max(1, ...displayValues))

    const batteryValues = points.map((p) => p.batteryV).filter((v): v is number => v !== undefined)
    const solarValues = points.map((p) => p.solarV).filter((v): v is number => v !== undefined)
    const voltageValues = [...batteryValues, ...solarValues]
    const voltageMin = voltageValues.length ? Math.min(...voltageValues) : 0
    const voltageMax = voltageValues.length ? Math.max(...voltageValues) : 20
    const voltageAxis = buildNiceAxisRange(voltageMin, voltageMax, true)

    const toPath = (values: number[], axisMin: number, axisMax: number) =>
      values
        .map((v, i) => {
          const x = padLeft + (i / (values.length - 1)) * plotW
          const y = padTop + (1 - (v - axisMin) / Math.max(0.0001, axisMax - axisMin)) * plotH
          return `${i === 0 ? 'M' : 'L'}${x.toFixed(2)},${y.toFixed(2)}`
        })
        .join(' ')

    const windD = toPath(displayValues, 0, windAxis.top)
    const batteryD =
      batteryValues.length === points.length ? toPath(points.map((p) => p.batteryV ?? 0), voltageAxis.min, voltageAxis.max) : ''
    const solarD =
      solarValues.length === points.length ? toPath(points.map((p) => p.solarV ?? 0), voltageAxis.min, voltageAxis.max) : ''

    const grid = Array.from({ length: windAxis.tickCount + 1 }, (_, i) => {
      const frac = i / windAxis.tickCount
      return {
        y: padTop + frac * plotH,
        value: windAxis.top - i * windAxis.step,
      }
    })

    const rightTicks = Array.from({ length: voltageAxis.tickCount + 1 }, (_, i) => {
      const frac = i / voltageAxis.tickCount
      return {
        y: padTop + frac * plotH,
        value: voltageAxis.max - frac * (voltageAxis.max - voltageAxis.min),
      }
    })

    const oldestTs = points[0].ts
    const latestTs = points[points.length - 1].ts
    const spanTs = Math.max(1, latestTs - oldestTs)

    // ESP has no RTC, so map relative sample offsets onto browser wall clock.
    const ticks = [0, 1, 2, 3, 4].map((i) => {
      const frac = i / 4
      const x = padLeft + frac * plotW
      const pointTs = oldestTs + Math.round(frac * spanTs)
      const labelMs = nowMs - (latestTs - pointTs) * 1000
      return { x, label: formatTs(labelMs) }
    })

    return { windPath: windD, batteryPath: batteryD, solarPath: solarD, yGrid: grid, yRightTicks: rightTicks, xTicks: ticks }
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
      <line className="chartAxis" x1="926" y1="10" x2="926" y2="266" />
      <line className="chartAxis" x1="58" y1="266" x2="926" y2="266" />
      {yGrid.map((g) => (
        <g key={`y-${g.y}`}>
          <line className="chartGrid" x1="58" y1={g.y} x2="926" y2={g.y} />
          <text className="chartAxisLabel" x="52" y={g.y + 4} textAnchor="end">
            {g.value.toFixed(1)}
          </text>
        </g>
      ))}
      {yRightTicks.map((g) => (
        <g key={`yr-${g.y}`}>
          <line className="chartTick" x1="926" y1={g.y} x2="932" y2={g.y} />
          <text className="chartAxisLabel chartAxisLabelRight" x="936" y={g.y + 4} textAnchor="start">
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
      <path d={windPath} fill="none" stroke="url(#lineGradient)" strokeWidth="4" />
      <path d={batteryPath} fill="none" stroke="#d62828" strokeWidth="2.5" />
      <path d={solarPath} fill="none" stroke="#f4a261" strokeWidth="2.5" />
      <text className="chartLegend" x="70" y="24">Wind</text>
      <text className="chartLegend chartLegendBattery" x="140" y="24">Battery V</text>
      <text className="chartLegend chartLegendSolar" x="246" y="24">Solar V</text>
    </svg>
  )
}

function App() {
  const [current, setCurrent] = useState<CurrentResponse | null>(null)
  const [historyPoints, setHistoryPoints] = useState<WindPoint[]>([])
  const [selectedRange, setSelectedRange] = useState<RangeKey>('4h')
  const [selectedUnit, setSelectedUnit] = useState<DisplayUnit>('kmh')
  const [browserNowMs, setBrowserNowMs] = useState(() => Date.now())
  const [error, setError] = useState<string | null>(null)
  const [wifiStatus, setWifiStatus] = useState<WifiStatus | null>(null)
  const [wifiSsid, setWifiSsid] = useState('')
  const [wifiPassword, setWifiPassword] = useState('')
  const [wifiMessage, setWifiMessage] = useState<string | null>(null)
  const [sensorStatus, setSensorStatus] = useState<SensorStatus | null>(null)
  const [sensorMessage, setSensorMessage] = useState<string | null>(null)
  const [showSettings, setShowSettings] = useState(false)

  const selectedRangeOption =
    RANGE_OPTIONS.find((option) => option.key === selectedRange) ?? RANGE_OPTIONS[3]

  useEffect(() => {
    const timer = window.setInterval(() => setBrowserNowMs(Date.now()), 30000)
    return () => window.clearInterval(timer)
  }, [])

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

    const loadHistory = async () => {
      try {
        const history = await provider.getHistory(selectedRangeOption.seconds)
        if (!cancelled) {
          setHistoryPoints(history.points)
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

    const loadSensorStatus = async () => {
      try {
        const res = await fetch('/api/sensor/status')
        if (!res.ok) {
          throw new Error(`HTTP ${res.status}`)
        }
        const data = (await res.json()) as SensorStatus
        if (!cancelled) {
          setSensorStatus(data)
        }
      } catch {
        if (!cancelled) {
          setSensorStatus(null)
        }
      }
    }

    loadCurrent()
    loadHistory()
    loadWifiStatus()
    loadSensorStatus()

    const currentTimer = window.setInterval(loadCurrent, 5000)
    const historyTimer = window.setInterval(loadHistory, 5000)
    const wifiTimer = window.setInterval(loadWifiStatus, 5000)
    const sensorTimer = window.setInterval(loadSensorStatus, 1000)

    return () => {
      cancelled = true
      window.clearInterval(currentTimer)
      window.clearInterval(historyTimer)
      window.clearInterval(wifiTimer)
      window.clearInterval(sensorTimer)
    }
  }, [selectedRangeOption.seconds])

  const latestMps = current?.mps ?? historyPoints.at(-1)?.mps
  const latestBatteryV = current?.batteryV ?? historyPoints.at(-1)?.batteryV
  const latestSolarV = current?.solarV ?? historyPoints.at(-1)?.solarV

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

  const onStartCalibration = async () => {
    setSensorMessage('Starting calibration (10s)... rotate sensor during this period.')
    try {
      const res = await fetch('/api/sensor/calibrate/start?seconds=10', { method: 'POST' })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      setSensorMessage('Calibration started. Keep rotor moving for 10 seconds.')
    } catch (err) {
      setSensorMessage(`Failed to start calibration: ${(err as Error).message}`)
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
            <h2>Battery</h2>
            <p>{formatVoltage(latestBatteryV)}</p>
          </article>
          <article>
            <h2>Solar</h2>
            <p>{formatVoltage(latestSolarV)}</p>
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
          <select
            className="rangeSelect"
            value={selectedRange}
            onChange={(e) => setSelectedRange(e.target.value as RangeKey)}
          >
            {RANGE_OPTIONS.map((option) => (
              <option key={option.key} value={option.key}>
                {option.label}
              </option>
            ))}
          </select>
        </header>
        <TinyChart points={historyPoints} unit={selectedUnit} nowMs={browserNowMs} />
        <p className="meta">
          {selectedRangeOption.label} | {historyPoints.length} points | timestamps mapped to browser
          local clock
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
            <hr />
            <p className="meta">
              Sensor mode: {sensorStatus?.mode ?? 'unknown'} | signal={sensorStatus?.signal ?? '--'} |
              threshold={sensorStatus?.threshold ?? '--'} | baseline={sensorStatus?.baseline ?? '--'} |
              reflected={sensorStatus?.reflected ?? '--'}
            </p>
            <p className="meta">
              calibrating={sensorStatus?.calibrating ? 'yes' : 'no'} | min=
              {sensorStatus?.calibrationMin ?? '--'} | max={sensorStatus?.calibrationMax ?? '--'}
            </p>
            <div className="buttons">
              <button type="button" onClick={onStartCalibration}>
                Start Sensor Calibration
              </button>
            </div>
            {sensorMessage && <p className="meta">{sensorMessage}</p>}
          </article>
        </section>
      )}
    </main>
  )
}

export default App
