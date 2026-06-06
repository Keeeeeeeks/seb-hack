import { useCallback, useMemo, useState } from 'react';
import type { TelemetryFrame } from './types';

const MAX_FRAMES = 1_800;

export function useTelemetryHistory() {
  const [frames, setFrames] = useState<TelemetryFrame[]>([]);

  const pushFrame = useCallback((frame: TelemetryFrame) => {
    setFrames((prev) => {
      const next = prev.length >= MAX_FRAMES ? prev.slice(prev.length - MAX_FRAMES + 1) : prev.slice();
      next.push(frame);
      return next;
    });
  }, []);

  const latest = frames.length > 0 ? frames[frames.length - 1] : null;

  const stats = useMemo(() => ({
    frameCount: frames.length,
    ageMs: latest?.t != null ? Date.now() - latest.t : null,
  }), [frames.length, latest?.t]);

  return { frames, latest, pushFrame, stats };
}
