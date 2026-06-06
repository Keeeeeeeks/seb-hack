export type VoiceTelemetry = {
  hz: number
  g: number
}

export type SeqTelemetry = {
  bpm: number
  step: number
  len: number
  on: boolean
}

export type TelemetryFrame = {
  t?: number
  temp_c?: number
  synth_hz?: number
  detune_c?: number
  voices?: VoiceTelemetry[]
  seq?: SeqTelemetry
  roll?: number
  pitch?: number
}
