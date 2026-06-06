export type StatusFlags = {
  mpu?: number;
  mcp?: number;
  sr04?: number;
  audio?: number;
  servo?: number;
  i2c_err?: number;
};

export type VoiceState = {
  hz?: number;
  g?: number;
};

export type SequencerState = {
  bpm?: number;
  step?: number;
  len?: number;
  on?: boolean;
};

export type TelemetryFrame = {
  t?: number;
  hb?: number;
  roll?: number;
  pitch?: number;
  ax?: number;
  ay?: number;
  az?: number;
  dist_mm?: number;
  dist_cm?: number;
  temp_c?: number;
  synth_hz?: number;
  filt_hz?: number;
  servo_deg?: number;
  status?: StatusFlags;
  voices?: VoiceState[];
  seq?: SequencerState;
  detune_c?: number;
  [key: string]: unknown;
};

export type SerialCommand =
  | { cmd: 'selftest' }
  | { cmd: 'set'; filt_hz?: number; master?: number }
  | { cmd: 'seq'; bpm?: number; on?: boolean };
