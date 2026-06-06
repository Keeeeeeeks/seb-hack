import type { TelemetryFrame } from './types';

function formatNumber(value: unknown, suffix = '', digits = 1) {
  return typeof value === 'number' && Number.isFinite(value) ? `${value.toFixed(digits)}${suffix}` : '—';
}

export function MetricGrid({ latest }: { latest: TelemetryFrame | null }) {
  const distanceMm = latest?.dist_mm ?? (typeof latest?.dist_cm === 'number' ? latest.dist_cm * 10 : undefined);
  const metrics = [
    ['Distance', formatNumber(distanceMm, ' mm', 0)],
    ['Roll', formatNumber(latest?.roll, '°')],
    ['Pitch', formatNumber(latest?.pitch, '°')],
    ['Temp', formatNumber(latest?.temp_c, ' °C')],
    ['Synth', formatNumber(latest?.synth_hz, ' Hz')],
    ['Filter', formatNumber(latest?.filt_hz, ' Hz')],
    ['Servo', formatNumber(latest?.servo_deg, '°')],
    ['Detune', formatNumber(latest?.detune_c, '¢')],
  ];

  return (
    <section className="metric-grid" aria-label="Latest telemetry values">
      {metrics.map(([label, value]) => (
        <div className="metric-card" key={label}>
          <span>{label}</span>
          <strong>{value}</strong>
        </div>
      ))}
    </section>
  );
}
