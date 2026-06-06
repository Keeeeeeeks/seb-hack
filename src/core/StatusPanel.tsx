import type { TelemetryFrame } from './types';

type Props = {
  latest: TelemetryFrame | null;
  connected: boolean;
  replaying: boolean;
};

const SENSOR_LABELS: Array<[keyof NonNullable<TelemetryFrame['status']>, string]> = [
  ['mpu', 'MPU-6050'],
  ['mcp', 'MCP9808'],
  ['sr04', 'HC-SR04'],
  ['audio', 'Audio'],
  ['servo', 'Servo'],
];

function statusText(flag: number | undefined) {
  if (flag == null) return { label: 'waiting', className: 'waiting' };
  return flag === 0 ? { label: 'ok', className: 'ok' } : { label: 'fault', className: 'fault' };
}

export function StatusPanel({ latest, connected, replaying }: Props) {
  const status = latest?.status;
  return (
    <section className="panel status-panel" aria-label="System status">
      <div className="panel-heading">
        <div>
          <p className="eyebrow">Air-Synth core</p>
          <h2>Status</h2>
        </div>
        <span className={`pill ${connected ? 'ok' : replaying ? 'waiting' : 'fault'}`}>
          {connected ? 'serial live' : replaying ? 'replay' : 'offline'}
        </span>
      </div>

      <div className="status-grid">
        {SENSOR_LABELS.map(([key, label]) => {
          const state = statusText(status?.[key]);
          return (
            <div className="status-row" key={key}>
              <span>{label}</span>
              <span className={`pill ${state.className}`}>{state.label}</span>
            </div>
          );
        })}
        <div className="status-row">
          <span>I2C errors</span>
          <span className="mono">{status?.i2c_err ?? '—'}</span>
        </div>
      </div>
    </section>
  );
}
