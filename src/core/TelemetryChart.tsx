import { useEffect, useMemo, useRef } from 'react';
import uPlot, { type AlignedData, type Options } from 'uplot';
import type { TelemetryFrame } from './types';

type NumericField = keyof Pick<TelemetryFrame, 'roll' | 'pitch' | 'dist_mm' | 'temp_c' | 'synth_hz' | 'filt_hz' | 'servo_deg' | 'detune_c'>;

type SeriesConfig = {
  field: NumericField;
  label: string;
  stroke: string;
};

type Props = {
  title: string;
  frames: TelemetryFrame[];
  series: SeriesConfig[];
  height?: number;
};

function toNumber(value: unknown): number | null {
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
}

export function TelemetryChart({ title, frames, series, height = 148 }: Props) {
  const rootRef = useRef<HTMLDivElement | null>(null);
  const plotRef = useRef<uPlot | null>(null);

  const data = useMemo<AlignedData>(() => {
    const x = frames.map((frame, index) => (typeof frame.t === 'number' ? frame.t / 1000 : index / 30));
    const ys = series.map(({ field }) => frames.map((frame) => toNumber(frame[field])));
    return [x, ...ys] as AlignedData;
  }, [frames, series]);

  useEffect(() => {
    const root = rootRef.current;
    if (!root) return;

    const opts: Options = {
      title,
      width: root.clientWidth || 640,
      height,
      cursor: { sync: { key: 'air-synth-core' } },
      scales: { x: { time: false } },
      axes: [
        { label: 't (s)', stroke: '#8b949e', grid: { stroke: '#263142', width: 1 } },
        { stroke: '#8b949e', grid: { stroke: '#263142', width: 1 } },
      ],
      series: [
        {},
        ...series.map((item) => ({
          label: item.label,
          stroke: item.stroke,
          width: 2,
          points: { show: false },
        })),
      ],
    };

    const plot = new uPlot(opts, data, root);
    plotRef.current = plot;

    const resizeObserver = new ResizeObserver(([entry]) => {
      plot.setSize({ width: Math.floor(entry.contentRect.width), height });
    });
    resizeObserver.observe(root);

    return () => {
      resizeObserver.disconnect();
      plot.destroy();
      plotRef.current = null;
    };
  }, [height, title, series]);

  useEffect(() => {
    plotRef.current?.setData(data);
  }, [data]);

  return <div ref={rootRef} className="chart-card" aria-label={`${title} chart`} />;
}
