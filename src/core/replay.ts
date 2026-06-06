import type { TelemetryFrame } from './types';

export function makeReplayFrame(startMs: number, nowMs: number): TelemetryFrame {
  const elapsed = (nowMs - startMs) / 1000;
  const phase = elapsed * Math.PI * 2;
  const dist = 260 + 180 * Math.sin(phase * 0.18);
  const roll = 35 * Math.sin(phase * 0.11);
  const pitch = 28 * Math.cos(phase * 0.14);
  const temp = 25 + 5 * Math.sin(phase * 0.045);
  const detune = Math.max(-75, Math.min(75, (temp - 25) * 15));
  const synth = 220 * Math.pow(2, Math.max(0, Math.min(1, (dist - 80) / 420)) * 2);
  const detunedSynth = synth * Math.pow(2, detune / 1200);

  return {
    t: nowMs,
    roll,
    pitch,
    ax: Math.sin(phase * 0.11),
    ay: Math.cos(phase * 0.11),
    az: 0.98,
    dist_mm: Math.round(dist),
    temp_c: temp,
    synth_hz: synth,
    detune_c: detune,
    filt_hz: 200 + ((pitch + 45) / 90) * 7800,
    servo_deg: Math.max(0, Math.min(180, (dist / 500) * 180)),
    status: { mpu: 0, mcp: 0, sr04: 0, audio: 0, servo: 0, i2c_err: 0 },
    voices: [{ hz: detunedSynth, g: 0.8 }],
  };
}
