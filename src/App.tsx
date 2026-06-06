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
import { TempDetune } from './showpiece/TempDetune';
import type { TelemetryFrame as ShowpieceTelemetryFrame } from './showpiece/telemetry/types';

function toShowpieceFrame(frame: TelemetryFrame | null): ShowpieceTelemetryFrame | null {
  if (!frame || typeof frame.detune_c !== 'number') return null;

  return {
    t: frame.t,
    temp_c: typeof frame.temp_c === 'number' ? frame.temp_c : undefined,
    synth_hz: typeof frame.synth_hz === 'number' ? frame.synth_hz : undefined,
    detune_c: frame.detune_c,
    voices: frame.voices?.map((voice) => ({ hz: voice.hz ?? 0, g: voice.g ?? 0 })),
    roll: typeof frame.roll === 'number' ? frame.roll : undefined,
    pitch: typeof frame.pitch === 'number' ? frame.pitch : undefined,
  };
}

export function App() {
  const { frames, latest, pushFrame } = useTelemetryHistory();
  const [replaying, setReplaying] = useState(true);
  const replayStartRef = useRef(Date.now());
  const showpieceFrame = toShowpieceFrame(latest);

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
          <p className="eyebrow">Combined Track A + Track B</p>
          <h1>Air-Synth control room</h1>
          <p className="hero-copy">
            Track A streams the playable instrument core. Track B adds the room-temperature detune
            showpiece. This page proves both telemetry surfaces are wired together.
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

      <div className="integrated-grid">
        <div className="dashboard-stack">
          <StatusPanel latest={latest} connected={serial.connected} replaying={replaying && !serial.connected} />
          <CommandBox disabled={!serial.connected} onSend={sendCommand} />
        </div>
        <section className="panel showpiece-panel" aria-label="Track B room detune showpiece">
          <div className="panel-heading">
            <div>
              <p className="eyebrow">Track B S1</p>
              <h2>Room detune showpiece</h2>
            </div>
            <span className={`pill ${showpieceFrame ? 'ok' : 'waiting'}`}>{showpieceFrame ? 'active' : 'waiting'}</span>
          </div>
          {showpieceFrame ? (
            <TempDetune frame={showpieceFrame} />
          ) : (
            <p className="empty-state">Waiting for <code>detune_c</code> telemetry. Replay mode emits it now.</p>
          )}
        </section>
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
