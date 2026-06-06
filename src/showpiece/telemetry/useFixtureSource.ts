import { useEffect, useMemo, useRef, useState } from 'react'
import type { TelemetryFrame } from './types'

export function useFixtureSource(frames: TelemetryFrame[], hz = 30): TelemetryFrame | null {
  const safeFrames = useMemo(() => frames.filter(Boolean), [frames])
  const [frame, setFrame] = useState<TelemetryFrame | null>(safeFrames[0] ?? null)
  const idx = useRef(0)

  useEffect(() => {
    if (safeFrames.length === 0) return
    const intervalMs = 1000 / hz
    const id = window.setInterval(() => {
      setFrame(safeFrames[idx.current % safeFrames.length])
      idx.current += 1
    }, intervalMs)
    return () => window.clearInterval(id)
  }, [safeFrames, hz])

  return frame
}
