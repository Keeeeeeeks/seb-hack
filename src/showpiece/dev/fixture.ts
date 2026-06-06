import type { TelemetryFrame } from '../telemetry/types'

function centsForTemp(tempC: number) {
  return Math.max(-75, Math.min(75, (tempC - 25) * 15))
}

export const fixtureFrames: TelemetryFrame[] = Array.from({ length: 180 }, (_, i) => {
  const temp_c = i < 60 ? 25 + (i / 60) * 5 : i < 120 ? 30 - ((i - 60) / 60) * 10 : 20 + ((i - 120) / 60) * 5
  const detune_c = centsForTemp(temp_c)
  const hz = 440 * Math.pow(2, detune_c / 1200)
  return {
    t: Math.round(i * (1000 / 30)),
    temp_c,
    synth_hz: hz,
    detune_c,
    voices: [{ hz, g: 0.2 }],
  }
})
