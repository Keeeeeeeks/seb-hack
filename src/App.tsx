import { useCallback, useEffect, useRef, useState } from 'react';
import { CommandBox } from './core/CommandBox';
import { IfField } from './core/IfField';
import { MetricGrid } from './core/MetricGrid';
import { makeReplayFrame } from './core/replay';
import { StatusPanel } from './core/StatusPanel';
import { TelemetryChart } from './core/TelemetryChart';
import { useSerialSource } from './core/useSerialSource';
import { useTelemetryHistory } from './core/useTelemetryHistory';
import type { SerialCommand, TelemetryFrame } from './core/types';

export function App() {
  const { frames, latest, pushFrame } = useTelemetryHistory();
  const [replaying, setReplaying] = useState(true);
  const replayStartRef = useRef(Date.now());

  const handleFrame = useCallback((frame: TelemetryFrame) => {
    pushFrame(frame);
  }, [pushFrame]);

  const serial = useSerialSource(handleFrame);

  useEffect(() => {
    if (!replaying || serial.connected) return;
    replayStartRef.current = Date.now();
    const id = window.setInterval(() => {
      handleFrame(makeReplayFrame(replayStartRef.current, Date.now()));
    }, 33);
    return () => window.clearInterval(id);
  }, [handleFrame, replaying, serial.connected]);

  async function sendCommand(cmd: SerialCommand) {
    await serial.sendCommand(cmd);
  }

  return (
    <main className="app-shell">
      <header className="hero">
        <div>
          <p className="eyebrow">Track A · core dashboard</p>
          <h1>Air-Synth telemetry</h1>
          <p className="hero-copy">
            Live Web Serial NDJSON for the NUCLEO-G474RE instrument, with replay mode when the board
            is not connected.
          </p>
        </div>
        <div className="hero-actions">
          <button type="button" onClick={() => void serial.connect()} disabled={!serial.supported || serial.connecting || serial.connected}>
            {serial.connecting ? 'Connecting…' : 'Connect board'}
          </button>
          <button type="button" onClick={() => void serial.disconnect()} disabled={!serial.connected}>
            Disconnect
          </button>
          <button type="button" className={replaying ? 'secondary active' : 'secondary'} onClick={() => setReplaying((v) => !v)}>
            Replay {replaying ? 'on' : 'off'}
          </button>
        </div>
      </header>

      {serial.error ? <div className="alert" role="alert">{serial.error}</div> : null}
      {!serial.supported ? <div className="alert" role="alert">Web Serial is available in Chrome/Edge on localhost or HTTPS.</div> : null}

      <div className="dashboard-grid">
        <StatusPanel latest={latest} connected={serial.connected} replaying={replaying && !serial.connected} />
        <CommandBox disabled={!serial.connected} onSend={sendCommand} />
      </div>

      <MetricGrid latest={latest} />

      <section className="charts" aria-label="Telemetry charts">
        <TelemetryChart
          title="Roll / pitch"
          frames={frames}
          series={[
            { field: 'roll', label: 'roll', stroke: '#58a6ff' },
            { field: 'pitch', label: 'pitch', stroke: '#f2cc60' },
          ]}
        />
        <TelemetryChart
          title="Distance / temperature"
          frames={frames}
          series={[
            { field: 'dist_mm', label: 'distance mm', stroke: '#7ee787' },
            { field: 'temp_c', label: 'temp C', stroke: '#ff7b72' },
          ]}
        />
        <TelemetryChart
          title="Synth / filter / servo"
          frames={frames}
          series={[
            { field: 'synth_hz', label: 'synth Hz', stroke: '#d2a8ff' },
            { field: 'filt_hz', label: 'filter Hz', stroke: '#79c0ff' },
            { field: 'servo_deg', label: 'servo deg', stroke: '#ffa657' },
          ]}
        />
      </section>

      <IfField frame={latest} field="voices">
        {(voices) => (
          <section className="panel">
            <div className="panel-heading">
              <div>
                <p className="eyebrow">M2 / Track B aware</p>
                <h2>Voices</h2>
              </div>
            </div>
            <div className="voice-list">
              {voices.map((voice, index) => (
                <span className="voice-pill" key={index}>v{index}: {(voice.hz ?? 0).toFixed(1)} Hz · {(voice.g ?? 0).toFixed(2)}</span>
              ))}
            </div>
          </section>
        )}
      </IfField>
    </main>
  );
}
