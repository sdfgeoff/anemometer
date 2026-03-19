export type WindPoint = {
  ts: number
  mps: number
  batteryV?: number
  solarV?: number
}

export type HistoryResponse = {
  seconds: number
  count: number
  points: WindPoint[]
}

export type CurrentResponse = {
  ok: boolean
  hasData: boolean
  source?: string
  sampleIntervalSeconds?: number
  ts?: number
  mps?: number
  batteryV?: number
  solarV?: number
}

export interface WindDataProvider {
  getCurrent(): Promise<CurrentResponse>
  getHistory(seconds: number): Promise<HistoryResponse>
}

class DummyWindProvider implements WindDataProvider {
  private readonly sampleIntervalSeconds = 5
  private readonly weekSamples = (7 * 24 * 60 * 60) / this.sampleIntervalSeconds
  private samples: WindPoint[]

  constructor() {
    const now = Math.floor(Date.now() / 1000)
    this.samples = Array.from({ length: this.weekSamples }, (_, i) => {
      const ts = now - (this.weekSamples - i) * this.sampleIntervalSeconds
      return {
        ts,
        mps: this.synthMps(ts),
        batteryV: this.synthBatteryV(ts),
        solarV: this.synthSolarV(ts),
      }
    })
  }

  async getCurrent(): Promise<CurrentResponse> {
    this.rollIfNeeded()
    const latest = this.samples[this.samples.length - 1]
    return {
      ok: true,
      hasData: true,
      source: 'dummy-ui',
      sampleIntervalSeconds: this.sampleIntervalSeconds,
      ts: latest.ts,
      mps: latest.mps,
      batteryV: latest.batteryV,
      solarV: latest.solarV,
    }
  }

  async getHistory(seconds: number): Promise<HistoryResponse> {
    this.rollIfNeeded()
    const wanted = Math.max(1, Math.floor(seconds / this.sampleIntervalSeconds))
    const points = this.samples.slice(-wanted)

    return {
      seconds,
      count: points.length,
      points,
    }
  }

  private rollIfNeeded() {
    const now = Math.floor(Date.now() / 1000)
    const latestTs = this.samples[this.samples.length - 1]?.ts ?? 0

    let nextTs = latestTs + this.sampleIntervalSeconds
    while (nextTs <= now) {
      this.samples.push({
        ts: nextTs,
        mps: this.synthMps(nextTs),
        batteryV: this.synthBatteryV(nextTs),
        solarV: this.synthSolarV(nextTs),
      })
      if (this.samples.length > this.weekSamples) {
        this.samples.shift()
      }
      nextTs += this.sampleIntervalSeconds
    }
  }

  private synthMps(tsSeconds: number): number {
    const t = tsSeconds
    const slow = 3.8 + 2.5 * Math.sin((2 * Math.PI * t) / 380)
    const gust = 1.5 * Math.sin((2 * Math.PI * t) / 62)
    const noise = (Math.random() - 0.5) * 0.4
    return Math.max(0, slow + gust + noise)
  }

  private synthBatteryV(tsSeconds: number): number {
    const solar = this.synthSolarV(tsSeconds)
    const chargeBias = solar > 5 ? 0.35 : -0.2
    const ripple = 0.06 * Math.sin((2 * Math.PI * tsSeconds) / 1800)
    return Math.max(10.5, Math.min(14.8, 12.6 + chargeBias + ripple))
  }

  private synthSolarV(tsSeconds: number): number {
    const daySeconds = ((tsSeconds % 86400) + 86400) % 86400
    const daylight = Math.max(0, Math.sin(((daySeconds - 6 * 3600) / (12 * 3600)) * Math.PI))
    const cloud = 0.25 * Math.sin((2 * Math.PI * tsSeconds) / 1700)
    return Math.max(0, 18.0 * daylight + 2.0 * cloud)
  }
}

class HttpWindProvider implements WindDataProvider {
  async getCurrent(): Promise<CurrentResponse> {
    return this.fetchJson<CurrentResponse>('/api/current')
  }

  async getHistory(seconds: number): Promise<HistoryResponse> {
    return this.fetchJson<HistoryResponse>(`/api/history?seconds=${seconds}`)
  }

  private async fetchJson<T>(path: string): Promise<T> {
    const res = await fetch(path)
    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`)
    }
    return (await res.json()) as T
  }
}

class AutoWindProvider implements WindDataProvider {
  private readonly http = new HttpWindProvider()
  private readonly dummy = new DummyWindProvider()
  private useDummy = false

  async getCurrent(): Promise<CurrentResponse> {
    if (this.useDummy) return this.dummy.getCurrent()
    try {
      return await this.http.getCurrent()
    } catch {
      this.useDummy = true
      return this.dummy.getCurrent()
    }
  }

  async getHistory(seconds: number): Promise<HistoryResponse> {
    if (this.useDummy) return this.dummy.getHistory(seconds)
    try {
      return await this.http.getHistory(seconds)
    } catch {
      this.useDummy = true
      return this.dummy.getHistory(seconds)
    }
  }
}

export function createWindDataProvider(): WindDataProvider {
  const mode = (import.meta.env.VITE_DATA_SOURCE ?? 'auto').toLowerCase()
  if (mode === 'dummy') return new DummyWindProvider()
  if (mode === 'http') return new HttpWindProvider()
  return new AutoWindProvider()
}
