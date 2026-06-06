/**
 * App.jsx — Air-Synth Dashboard root
 *
 * Renders core panels (SerialConnect, StatusPanel, Charts, SynthScope, CommandBox).
 * Conditionally renders showpiece panels ONLY when their telemetry fields appear
 * in the latest frame AND the component module exists (dynamic import, Track B owns
 * src/showpiece/*).
 *
 * Track A must NOT hard-import anything from src/showpiece/.
 */
import React, { useState, useCallback, useRef, useEffect, Suspense } from 'react';
import { SerialConnect } from './core/SerialConnect.jsx';
import { StatusPanel }   from './core/StatusPanel.jsx';
import { Charts }        from './core/Charts.jsx';
import { SynthScope }    from './core/SynthScope.jsx';
import { CommandBox }    from './core/CommandBox.jsx';

// ---------------------------------------------------------------------------
// Showpiece panel registry — Track B populates these modules.
// Each entry: { field, path, Component (lazy-loaded) }
// We attempt a dynamic import once; if the module doesn't exist we silently skip.
// ---------------------------------------------------------------------------
const SHOWPIECE_PANELS = [
  { field: 'voices',   path: './showpiece/VoicesPanel.jsx'    },
  { field: 'seq',      path: './showpiece/SequencerGrid.jsx'  },
  { field: 'detune_c', path: './showpiece/TempDetune.jsx'     },
];

/**
 * Attempt to lazy-load a showpiece component.
 * Returns a React.lazy component, or null if the import fails.
 */
function tryLazyImport(path) {
  // React.lazy requires a function that returns a Promise<{ default: Component }>
  // We wrap in a try/catch-on-rejection so a missing module doesn't crash the app.
  return React.lazy(() =>
    import(/* @vite-ignore */ path).catch(() => ({
      default: () => null, // render nothing if module absent
    }))
  );
}

// Pre-build lazy components once (not inside render)
const lazyPanels = SHOWPIECE_PANELS.map((p) => ({
  ...p,
  LazyComponent: tryLazyImport(p.path),
}));

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------
export default function App() {
  const [frame, setFrame]       = useState(null);
  const [lastTs, setLastTs]     = useState(null);
  const [writer, setWriter]     = useState(null);
  const writerRef               = useRef(null);

  const handleFrame = useCallback((obj) => {
    setFrame(obj);
    setLastTs(Date.now());
  }, []);

  const handleWriterReady = useCallback((w) => {
    writerRef.current = w;
    setWriter(w);
  }, []);

  const handleDisconnect = useCallback(() => {
    writerRef.current = null;
    setWriter(null);
  }, []);

  return (
    <div style={styles.page}>
      {/* ── Header ── */}
      <header style={styles.header}>
        <span style={styles.logo}>🎵 Air-Synth</span>
        <span style={styles.subtitle}>STM32G474 · CORDIC + FMAC · Web Serial Dashboard</span>
      </header>

      {/* ── Top bar: connect + status ── */}
      <div style={styles.topBar}>
        <SerialConnect
          onFrame={handleFrame}
          onWriterReady={handleWriterReady}
          onDisconnect={handleDisconnect}
        />
        <StatusPanel frame={frame} lastTs={lastTs} />
      </div>

      {/* ── Charts ── */}
      <div style={styles.section}>
        <Charts frame={frame} />
      </div>

      {/* ── SynthScope + CommandBox ── */}
      <div style={styles.row}>
        <div style={styles.halfCol}>
          <SynthScope frame={frame} />
        </div>
        <div style={styles.halfCol}>
          <CommandBox writer={writer} />
        </div>
      </div>

      {/* ── Showpiece panels (Track B) — rendered only when telemetry fields present ── */}
      {lazyPanels.map(({ field, path, LazyComponent }) => {
        // Only render if the field exists in the latest frame
        const fieldPresent = frame !== null && frame[field] !== undefined;
        if (!fieldPresent) return null;
        return (
          <div key={field} style={styles.section}>
            <Suspense fallback={<div style={styles.spFallback}>Loading {field} panel…</div>}>
              <LazyComponent frame={frame} />
            </Suspense>
          </div>
        );
      })}

      {/* ── Footer ── */}
      <footer style={styles.footer}>
        Track A core · Phase 5 · Web Serial (Chrome/Edge only)
      </footer>
    </div>
  );
}

// ---------------------------------------------------------------------------
// Styles
// ---------------------------------------------------------------------------
const styles = {
  page: {
    maxWidth: 1100,
    margin: '0 auto',
    padding: '16px 20px 40px',
    display: 'flex',
    flexDirection: 'column',
    gap: 16,
  },
  header: {
    display: 'flex',
    alignItems: 'baseline',
    gap: 12,
    borderBottom: '1px solid #21262d',
    paddingBottom: 12,
  },
  logo: {
    fontSize: 22,
    fontWeight: 800,
    color: '#e6edf3',
  },
  subtitle: {
    fontSize: 13,
    color: '#8b949e',
  },
  topBar: {
    display: 'flex',
    gap: 16,
    flexWrap: 'wrap',
    alignItems: 'flex-start',
  },
  section: {
    width: '100%',
  },
  row: {
    display: 'flex',
    gap: 16,
    flexWrap: 'wrap',
  },
  halfCol: {
    flex: '1 1 320px',
    minWidth: 0,
  },
  spFallback: {
    padding: 16,
    color: '#8b949e',
    fontSize: 13,
  },
  footer: {
    textAlign: 'center',
    fontSize: 11,
    color: '#484f58',
    marginTop: 8,
  },
};
