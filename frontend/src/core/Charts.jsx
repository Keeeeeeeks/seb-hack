/**
 * Charts.jsx
 *
 * uPlot rolling-window chart with a 10-second / 300-point buffer at ~30 Hz.
 * Series: roll (°), pitch (°), distance_mm, temp_c, synth_hz, servo_deg.
 * Auto-scales Y per series. Renders in a responsive container.
 *
 * Props:
 *   frame — latest telemetry object (or null); call addFrame externally via ref,
 *           or pass frame as a prop and let the component push it.
 */
import React, { useEffect, useRef, useCallback } from 'react';
import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';

const MAX_POINTS = 300; // 10 s × 30 Hz

// Series definitions: [key in telemetry, label, colour, scale]
const SERIES_DEF = [
  { key: 'roll',       label: 'Roll (°)',      stroke: '#58a6ff', scale: 'deg'  },
  { key: 'pitch',      label: 'Pitch (°)',     stroke: '#3fb950', scale: 'deg'  },
  { key: 'dist_mm',    label: 'Dist (mm)',     stroke: '#e3b341', scale: 'mm'   },
  { key: 'temp_c',     label: 'Temp (°C)',     stroke: '#f78166', scale: 'temp' },
  { key: 'synth_hz',   label: 'Synth (Hz)',    stroke: '#bc8cff', scale: 'hz'   },
  { key: 'servo_deg',  label: 'Servo (°)',     stroke: '#39d353', scale: 'sdeg' },
];

function makeEmptyData() {
  // [timestamps, ...series values]
  return [[], ...SERIES_DEF.map(() => [])];
}

export function Charts({ frame }) {
  const containerRef = useRef(null);
  const plotRef      = useRef(null);
  const dataRef      = useRef(makeEmptyData());

  // Build uPlot options
  const buildOptions = useCallback((width) => {
    const series = [
      { label: 'Time' },
      ...SERIES_DEF.map((s) => ({
        label:  s.label,
        stroke: s.stroke,
        width:  1.5,
        scale:  s.scale,
      })),
    ];

    // One scale per series key (auto-range)
    const scales = {};
    SERIES_DEF.forEach((s) => {
      scales[s.scale] = { auto: true };
    });

    // One axis per unique scale (deduplicated)
    const seenScales = new Set();
    const axes = [
      { scale: 'x', label: 'Time (s)', stroke: '#8b949e', grid: { stroke: '#21262d' } },
    ];
    SERIES_DEF.forEach((s) => {
      if (!seenScales.has(s.scale)) {
        seenScales.add(s.scale);
        axes.push({
          scale: s.scale,
          stroke: '#8b949e',
          grid: { stroke: '#21262d' },
          ticks: { stroke: '#30363d' },
          size: 50,
        });
      }
    });

    return {
      width,
      height: 280,
      series,
      scales,
      axes,
      cursor: { show: true },
      legend: { show: true },
    };
  }, []);

  // Initialize uPlot once container is mounted
  useEffect(() => {
    if (!containerRef.current) return;
    const width = containerRef.current.clientWidth || 800;
    const opts  = buildOptions(width);
    const plot  = new uPlot(opts, dataRef.current, containerRef.current);
    plotRef.current = plot;

    // Responsive resize
    const ro = new ResizeObserver((entries) => {
      for (const entry of entries) {
        const w = entry.contentRect.width;
        if (w > 0 && plotRef.current) {
          plotRef.current.setSize({ width: w, height: 280 });
        }
      }
    });
    ro.observe(containerRef.current);

    return () => {
      ro.disconnect();
      plot.destroy();
      plotRef.current = null;
    };
  }, [buildOptions]);

  // Push new frame into rolling buffer and redraw
  useEffect(() => {
    if (!frame || !plotRef.current) return;

    const data = dataRef.current;
    const tSec = (frame.t ?? Date.now()) / 1000;

    // Append timestamp
    data[0].push(tSec);

    // Append each series value (use NaN if field missing)
    SERIES_DEF.forEach((s, i) => {
      const v = frame[s.key];
      data[i + 1].push(v !== undefined && v !== null ? Number(v) : NaN);
    });

    // Trim to rolling window
    if (data[0].length > MAX_POINTS) {
      const trim = data[0].length - MAX_POINTS;
      data[0] = data[0].slice(trim);
      for (let i = 1; i < data.length; i++) {
        data[i] = data[i].slice(trim);
      }
    }

    plotRef.current.setData(data);
  }, [frame]);

  return (
    <div style={styles.wrapper}>
      <div style={styles.header}>Telemetry Charts — 10 s rolling window</div>
      <div ref={containerRef} style={styles.chartContainer} />
    </div>
  );
}

const styles = {
  wrapper: {
    background: '#161b22',
    border: '1px solid #30363d',
    borderRadius: 8,
    padding: '12px 16px',
    display: 'flex',
    flexDirection: 'column',
    gap: 8,
  },
  header: {
    fontWeight: 700,
    fontSize: 13,
    color: '#8b949e',
    textTransform: 'uppercase',
    letterSpacing: '0.05em',
  },
  chartContainer: {
    width: '100%',
    minHeight: 280,
  },
};

export default Charts;
