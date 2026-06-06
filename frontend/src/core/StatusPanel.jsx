/**
 * StatusPanel.jsx
 *
 * Displays per-sensor status (green/red dot), i2c_err count,
 * last-update age (goes red if >500 ms), and CORDIC/FMAC active badges.
 *
 * Props:
 *   frame  — latest telemetry object (or null)
 *   lastTs — Date.now() timestamp of last received frame (or null)
 */
import React, { useState, useEffect } from 'react';

const STALE_MS = 500;

const SENSOR_LABELS = {
  mpu:   'MPU-6050',
  mcp:   'MCP9808',
  sr04:  'HC-SR04',
  audio: 'Audio',
  servo: 'Servo',
};

function StatusDot({ ok, label }) {
  return (
    <div style={styles.sensorRow}>
      <span
        style={{
          ...styles.dot,
          background: ok ? '#3fb950' : '#f85149',
          boxShadow: ok ? '0 0 4px #3fb950aa' : '0 0 4px #f85149aa',
        }}
      />
      <span style={styles.sensorLabel}>{label}</span>
    </div>
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

export function StatusPanel({ frame, lastTs }) {
  const [now, setNow] = useState(Date.now());

  // Tick every 100 ms to update age display
  useEffect(() => {
    const id = setInterval(() => setNow(Date.now()), 100);
    return () => clearInterval(id);
  }, []);

  const status = frame?.status ?? null;
  const ageMs = lastTs ? now - lastTs : null;
  const stale = ageMs === null || ageMs > STALE_MS;

  // status.audio === 0 means audio OK; non-zero = error/inactive
  // CORDIC/FMAC "active" is inferred from status.audio === 0 (audio running)
  // Per SEB.MD: status flags are 0=OK, non-zero=error
  const cordicActive = status?.audio === 0;
  const fmacActive   = status?.audio === 0;

  return (
    <div style={styles.container}>
      <div style={styles.header}>System Status</div>

      {/* Per-sensor dots */}
      <div style={styles.sensorGrid}>
        {Object.entries(SENSOR_LABELS).map(([key, label]) => (
          <StatusDot
            key={key}
            label={label}
            ok={status ? status[key] === 0 : false}
          />
        ))}
      </div>

      {/* i2c_err count */}
      <div style={styles.metaRow}>
        <span style={styles.metaLabel}>I²C errors</span>
        <span
          style={{
            ...styles.metaValue,
            color: (status?.i2c_err ?? 0) > 0 ? '#f85149' : '#3fb950',
          }}
        >
          {status?.i2c_err ?? '—'}
        </span>
      </div>

      {/* Last-update age */}
      <div style={styles.metaRow}>
        <span style={styles.metaLabel}>Last update</span>
        <span
          style={{
            ...styles.metaValue,
            color: stale ? '#f85149' : '#3fb950',
          }}
        >
          {ageMs !== null ? `${ageMs} ms ago` : 'no data'}
        </span>
      </div>

      {/* CORDIC / FMAC badges */}
      <div style={styles.badgeRow}>
        <Badge label="CORDIC active" active={cordicActive} color="#1f6feb" />
        <Badge label="FMAC active"   active={fmacActive}   color="#8957e5" />
      </div>
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
    minWidth: 200,
  },
  header: {
    fontWeight: 700,
    fontSize: 13,
    color: '#8b949e',
    textTransform: 'uppercase',
    letterSpacing: '0.05em',
  },
  sensorGrid: {
    display: 'flex',
    flexDirection: 'column',
    gap: 6,
  },
  sensorRow: {
    display: 'flex',
    alignItems: 'center',
    gap: 8,
  },
  dot: {
    width: 10,
    height: 10,
    borderRadius: '50%',
    flexShrink: 0,
  },
  sensorLabel: {
    fontSize: 13,
  },
  metaRow: {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    fontSize: 12,
    borderTop: '1px solid #21262d',
    paddingTop: 6,
  },
  metaLabel: {
    color: '#8b949e',
  },
  metaValue: {
    fontVariantNumeric: 'tabular-nums',
    fontWeight: 600,
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
};

export default StatusPanel;
