import type { TelemetryFrame } from '../telemetry/types'
import { CentsGauge } from './CentsGauge'

type Props = {
  frame: TelemetryFrame
}

function numberOrDash(value: number | undefined, digits = 1) {
  return typeof value === 'number' && Number.isFinite(value) ? value.toFixed(digits) : '—'
}

export function TempDetune({ frame }: Props) {
  const detune = typeof frame.detune_c === 'number' && Number.isFinite(frame.detune_c) ? frame.detune_c : 0
  const voiceHz = frame.voices?.[0]?.hz ?? frame.synth_hz

  return (
    <section
      className="showpiece-card temp-detune"
      aria-labelledby="temp-detune-title"
      style={{
        width: 320,
        borderRadius: 24,
        padding: 20,
        background: 'linear-gradient(145deg, rgba(15,23,42,0.96), rgba(12,74,110,0.72))',
        border: '1px solid rgba(125, 211, 252, 0.24)',
        boxShadow: '0 24px 80px rgba(2, 8, 23, 0.42)',
        color: '#f8fafc',
        fontFamily: 'Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif',
      }}
    >
      <p style={{ margin: '0 0 6px', color: '#7dd3fc', fontSize: 12, fontWeight: 700, letterSpacing: '0.12em', textTransform: 'uppercase' }}>S1 committed</p>
      <h2 id="temp-detune-title" style={{ margin: 0, fontSize: 26, lineHeight: 1.05 }}>Room Detune</h2>
      <p style={{ margin: '8px 0 16px', color: '#cbd5e1', fontSize: 14, lineHeight: 1.45 }}>
        Warmer air bends every voice sharper. The room is now part of the instrument.
      </p>
      <CentsGauge cents={detune} />
      <dl style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 10, margin: '14px 0 0' }}>
        <div><dt style={{ color: '#94a3b8', fontSize: 11 }}>Temp</dt><dd style={{ margin: 0, fontVariantNumeric: 'tabular-nums' }}>{numberOrDash(frame.temp_c)} °C</dd></div>
        <div><dt style={{ color: '#94a3b8', fontSize: 11 }}>Detune</dt><dd style={{ margin: 0, fontVariantNumeric: 'tabular-nums' }}>{detune.toFixed(1)}¢</dd></div>
        <div><dt style={{ color: '#94a3b8', fontSize: 11 }}>Voice 0</dt><dd style={{ margin: 0, fontVariantNumeric: 'tabular-nums' }}>{numberOrDash(voiceHz, 1)} Hz</dd></div>
      </dl>
    </section>
  )
}
