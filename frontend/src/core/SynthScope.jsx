/**
 * SynthScope.jsx
 *
 * Displays:
 *  - synth_hz and filt_hz numeric readouts
 *  - SVG sine wave preview (static shape, frequency label)
 *  - CORDIC active / FMAC active indicator badges
 *  - Filter cutoff gauge: horizontal bar 400–4000 Hz range
 *
 * Props:
 *   frame — latest telemetry object (or null)
 */
import React, { useMemo } from 'react';

const FILT_MIN = 400;
const FILT_MAX = 4000;
const SVG_W    = 300;
const SVG_H    = 80;
const CYCLES   = 3; // number of sine cycles shown in preview

function SinePreview({ hz }) {
  // Generate a static sine path; frequency label only, shape is always CYCLES cycles
  const points = useMemo(() => {
    const pts = [];
    const steps = 120;
    for (let i = 0; i <= steps; i++) {
      const x = (i / steps) * SVG_W;
      const y = SVG_H / 2 - (SVG_H * 0.38) * Math.sin((i / steps) * CYCLES * 2 * Math.PI);
      pts.push(`${i === 0 ? 'M' : 'L'}${x.toFixed(1)},${y.toFixed(1)}`);
    }
    return pts.join(' ');
  }, []); // static shape — only label changes

  return (
    <svg
      width="100%"
      viewBox={`0 0 ${SVG_W} ${SVG_H}`}
      style={styles.svg}
      aria-label={`Sine wave preview at ${hz ?? '—'} Hz`}
    >
      {/* Grid line */}
      <line x1="0" y1={SVG_H / 2} x2={SVG_W} y2={SVG_H / 2} stroke="#30363d" strokeWidth="1" />
      {/* Sine curve */}
      <path d={points} stroke="#58a6ff" strokeWidth="2" fill="none" />
      {/* Frequency label */}
      <text x={SVG_W - 4} y={14} textAnchor="end" fill="#8b949e" fontSize="11">
        {hz !== null && hz !== undefined ? `${Number(hz).toFixed(1)} Hz` : '— Hz'}
      </text>
    </svg>
  );
}

function Badge({ label, active, color }) {
  return (
    <span
      style={{
        ...styles.badge,
        background: active ? color : '#21262d',
        color: active ? '#fff' : '#8b949e',
        border: `1px solid ${active ? color : '#30363d'}`,
      }}
    >
      {label}
    </span>
  );
}

function FilterGauge({ hz }) {
  const clamped = Math.max(FILT_MIN, Math.min(FILT_MAX, hz ?? FILT_MIN));
  const pct = ((clamped - FILT_MIN) / (FILT_MAX - FILT_MIN)) * 100;

  return (
    <div style={styles.gaugeWrapper}>
      <div style={styles.gaugeLabels}>
        <span style={styles.gaugeTick}>{FILT_MIN} Hz</span>
        <span style={styles.gaugeTitle}>Filter Cutoff</span>
        <span style={styles.gaugeTick}>{FILT_MAX} Hz</span>
      </div>
      <div style={styles.gaugeTrack}>
        <div
          style={{
            ...styles.gaugeBar,
            width: `${pct.toFixed(1)}%`,
          }}
        />
        <div
          style={{
            ...styles.gaugeThumb,
            left: `calc(${pct.toFixed(1)}% - 6px)`,
          }}
        />
      </div>
      <div style={styles.gaugeValue}>
        {hz !== null && hz !== undefined ? `${Number(hz).toFixed(0)} Hz` : '— Hz'}
      </div>
    </div>
  );
}

export function SynthScope({ frame }) {
  const synthHz = frame?.synth_hz ?? null;
  const filtHz  = frame?.filt_hz  ?? null;
  // CORDIC/FMAC active when audio status is OK (0)
  const cordicActive = frame?.status?.audio === 0;
  const fmacActive   = frame?.status?.audio === 0;

  return (
    <div style={styles.container}>
      <div style={styles.header}>Synth Scope</div>

      {/* Numeric readouts */}
      <div style={styles.readoutRow}>
        <div style={styles.readout}>
          <span style={styles.readoutLabel}>Synth</span>
          <span style={styles.readoutValue}>
            {synthHz !== null ? `${Number(synthHz).toFixed(1)} Hz` : '—'}
          </span>
        </div>
        <div style={styles.readout}>
          <span style={styles.readoutLabel}>Filter</span>
          <span style={styles.readoutValue}>
            {filtHz !== null ? `${Number(filtHz).toFixed(0)} Hz` : '—'}
          </span>
        </div>
      </div>

      {/* SVG sine preview */}
      <SinePreview hz={synthHz} />

      {/* CORDIC / FMAC badges */}
      <div style={styles.badgeRow}>
        <Badge label="CORDIC active" active={cordicActive} color="#1f6feb" />
        <Badge label="FMAC active"   active={fmacActive}   color="#8957e5" />
      </div>

      {/* Filter cutoff gauge */}
      <FilterGauge hz={filtHz} />
    </div>
  );
}

const styles = {
  container: {
    background: '#161b22',
    border: '1px solid #30363d',
    borderRadius: 8,
    padding: '12px 16px',
    display: 'flex',
    flexDirection: 'column',
    gap: 10,
  },
  header: {
    fontWeight: 700,
    fontSize: 13,
    color: '#8b949e',
    textTransform: 'uppercase',
    letterSpacing: '0.05em',
  },
  readoutRow: {
    display: 'flex',
    gap: 24,
  },
  readout: {
    display: 'flex',
    flexDirection: 'column',
    gap: 2,
  },
  readoutLabel: {
    fontSize: 11,
    color: '#8b949e',
    textTransform: 'uppercase',
    letterSpacing: '0.05em',
  },
  readoutValue: {
    fontSize: 22,
    fontWeight: 700,
    fontVariantNumeric: 'tabular-nums',
    color: '#e6edf3',
  },
  svg: {
    display: 'block',
    background: '#0d1117',
    borderRadius: 4,
    border: '1px solid #21262d',
  },
  badgeRow: {
    display: 'flex',
    gap: 6,
    flexWrap: 'wrap',
  },
  badge: {
    fontSize: 11,
    fontWeight: 600,
    borderRadius: 4,
    padding: '2px 8px',
    letterSpacing: '0.03em',
  },
  gaugeWrapper: {
    display: 'flex',
    flexDirection: 'column',
    gap: 4,
  },
  gaugeLabels: {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  gaugeTitle: {
    fontSize: 11,
    color: '#8b949e',
    textTransform: 'uppercase',
    letterSpacing: '0.05em',
  },
  gaugeTick: {
    fontSize: 10,
    color: '#8b949e',
  },
  gaugeTrack: {
    position: 'relative',
    height: 8,
    background: '#21262d',
    borderRadius: 4,
    overflow: 'visible',
  },
  gaugeBar: {
    height: '100%',
    background: 'linear-gradient(90deg, #1f6feb, #8957e5)',
    borderRadius: 4,
    transition: 'width 0.1s ease',
  },
  gaugeThumb: {
    position: 'absolute',
    top: -4,
    width: 12,
    height: 16,
    background: '#e6edf3',
    borderRadius: 3,
    transition: 'left 0.1s ease',
  },
  gaugeValue: {
    textAlign: 'center',
    fontSize: 12,
    fontVariantNumeric: 'tabular-nums',
    color: '#e6edf3',
    fontWeight: 600,
  },
};

export default SynthScope;
