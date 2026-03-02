export type WindPoint = {
  ts: number
  mps: number
}

export type HistoryRange = '24h' | 'week'

export type HistoryResponse = {
  range: HistoryRange
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
}

export interface WindDataProvider {
  getCurrent(): Promise<CurrentResponse>
  getHistory(range: HistoryRange): Promise<HistoryResponse>
}

class DummyWindProvider implements WindDataProvider {
  private readonly sampleIntervalSeconds = 30
  private readonly weekSamples = 7 * 24 * 60 * 2
  private samples: WindPoint[]

  constructor() {
    const now = Math.floor(Date.now() / 1000)
    this.samples = Array.from({ length: this.weekSamples }, (_, i) => {
      const ts = now - (this.weekSamples - i) * this.sampleIntervalSeconds
      return { ts, mps: this.synthMps(ts) }
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
    }
  }

  async getHistory(range: HistoryRange): Promise<HistoryResponse> {
    this.rollIfNeeded()
    const points =
      range === '24h' ? this.samples.slice(-(24 * 60 * 2)) : this.samples.slice(-this.weekSamples)

    return {
      range,
      count: points.length,
      points,
    }
  }

  private rollIfNeeded() {
    const now = Math.floor(Date.now() / 1000)
    const latestTs = this.samples[this.samples.length - 1]?.ts ?? 0

    let nextTs = latestTs + this.sampleIntervalSeconds
    while (nextTs <= now) {
      this.samples.push({ ts: nextTs, mps: this.synthMps(nextTs) })
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
}

class HttpWindProvider implements WindDataProvider {
  async getCurrent(): Promise<CurrentResponse> {
    return this.fetchJson<CurrentResponse>('/api/current')
  }

  async getHistory(range: HistoryRange): Promise<HistoryResponse> {
    return this.fetchJson<HistoryResponse>(`/api/history?range=${range}`)
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

  async getHistory(range: HistoryRange): Promise<HistoryResponse> {
    if (this.useDummy) return this.dummy.getHistory(range)
    try {
      return await this.http.getHistory(range)
    } catch {
      this.useDummy = true
      return this.dummy.getHistory(range)
    }
  }
}

export function createWindDataProvider(): WindDataProvider {
  const mode = (import.meta.env.VITE_DATA_SOURCE ?? 'auto').toLowerCase()
  if (mode === 'dummy') return new DummyWindProvider()
  if (mode === 'http') return new HttpWindProvider()
  return new AutoWindProvider()
}
