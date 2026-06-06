/**
 * SerialConnect.jsx
 *
 * Manages Web Serial API connection at 115200 baud.
 * Pipes: ReadableStream → TextDecoderStream → line-splitter TransformStream → NdjsonParser.
 * Exposes a WritableStream writer for outbound commands.
 *
 * Props:
 *   onFrame(obj)          — called for each parsed telemetry frame
 *   onWriterReady(writer) — called with the serial WritableStreamDefaultWriter when connected
 *   onDisconnect()        — called when port closes
 */
import React, { useState, useRef, useCallback } from 'react';
import { NdjsonParser } from './NdjsonParser.js';

const BAUD_RATE = 115200;

/** TransformStream that splits a text stream into lines (splits on '\n'). */
function makeLineSplitter() {
  let buf = '';
  return new TransformStream({
    transform(chunk, controller) {
      buf += chunk;
      let nl;
      while ((nl = buf.indexOf('\n')) !== -1) {
        const line = buf.slice(0, nl + 1); // include the '\n'
        buf = buf.slice(nl + 1);
        controller.enqueue(line);
      }
    },
    flush(controller) {
      if (buf.length > 0) {
        controller.enqueue(buf);
        buf = '';
      }
    },
  });
}

export function SerialConnect({ onFrame, onWriterReady, onDisconnect }) {
  const [connected, setConnected] = useState(false);
  const [error, setError] = useState(null);
  const portRef = useRef(null);
  const readerRef = useRef(null);
  const writerRef = useRef(null);
  const abortRef = useRef(null);

  const connect = useCallback(async () => {
    setError(null);
    try {
      // Must be triggered by a user gesture — this function is called from onClick
      const port = await navigator.serial.requestPort();
      await port.open({ baudRate: BAUD_RATE });
      portRef.current = port;

      // Set up writable side for commands
      const writer = port.writable.getWriter();
      writerRef.current = writer;
      if (onWriterReady) onWriterReady(writer);

      // Set up readable pipeline
      const parser = new NdjsonParser((obj) => {
        if (onFrame) onFrame(obj);
      });

      const abortController = new AbortController();
      abortRef.current = abortController;

      const lineSplitter = makeLineSplitter();

      // Pipe: readable → TextDecoderStream → lineSplitter → reader
      const textDecoder = new TextDecoderStream();
      const pipePromise = port.readable
        .pipeTo(textDecoder.writable, { signal: abortController.signal })
        .catch(() => {}); // ignore abort errors

      const lineReader = textDecoder.readable
        .pipeThrough(lineSplitter)
        .getReader();

      readerRef.current = lineReader;
      setConnected(true);

      // Read loop — runs until disconnect
      (async () => {
        try {
          while (true) {
            const { value, done } = await lineReader.read();
            if (done) break;
            if (value) parser.push(value);
          }
        } catch (_e) {
          // Port closed or aborted — expected on disconnect
        } finally {
          parser.reset();
          setConnected(false);
          if (onDisconnect) onDisconnect();
        }
      })();

      await pipePromise;
    } catch (err) {
      setError(err.message || String(err));
      setConnected(false);
    }
  }, [onFrame, onWriterReady, onDisconnect]);

  const disconnect = useCallback(async () => {
    try {
      // Abort the pipe
      if (abortRef.current) {
        abortRef.current.abort();
        abortRef.current = null;
      }
      // Cancel the reader
      if (readerRef.current) {
        await readerRef.current.cancel().catch(() => {});
        readerRef.current = null;
      }
      // Release the writer
      if (writerRef.current) {
        await writerRef.current.releaseLock();
        writerRef.current = null;
      }
      // Close the port
      if (portRef.current) {
        await portRef.current.close().catch(() => {});
        portRef.current = null;
      }
    } catch (_e) {
      // Best-effort cleanup
    }
    setConnected(false);
    if (onDisconnect) onDisconnect();
  }, [onDisconnect]);

  const supported = typeof navigator !== 'undefined' && 'serial' in navigator;

  return (
    <div style={styles.container}>
      <div style={styles.row}>
        <span style={{ ...styles.dot, background: connected ? '#3fb950' : '#f85149' }} />
        <span style={styles.label}>{connected ? 'Connected' : 'Disconnected'}</span>
        <span style={styles.baud}>115200 baud</span>
      </div>

      {!supported && (
        <div style={styles.warning}>
          ⚠ Web Serial API not available. Use Chrome or Edge.
        </div>
      )}

      <div style={styles.row}>
        {!connected ? (
          <button
            style={styles.btn}
            onClick={connect}
            disabled={!supported}
            title="Select serial port (user gesture required)"
          >
            Connect
          </button>
        ) : (
          <button
            style={{ ...styles.btn, background: '#da3633' }}
            onClick={disconnect}
          >
            Disconnect
          </button>
        )}
      </div>

      {error && <div style={styles.error}>Error: {error}</div>}
    </div>
  );
}

const styles = {
  container: {
    background: '#161b22',
    border: '1px solid #30363d',
    borderRadius: 8,
    padding: '12px 16px',
    display: 'flex',
    flexDirection: 'column',
    gap: 8,
    minWidth: 220,
  },
  row: {
    display: 'flex',
    alignItems: 'center',
    gap: 8,
  },
  dot: {
    width: 10,
    height: 10,
    borderRadius: '50%',
    flexShrink: 0,
  },
  label: {
    fontWeight: 600,
    fontSize: 14,
  },
  baud: {
    fontSize: 11,
    color: '#8b949e',
    marginLeft: 'auto',
  },
  btn: {
    background: '#238636',
    color: '#fff',
    border: 'none',
    borderRadius: 6,
    padding: '6px 16px',
    cursor: 'pointer',
    fontSize: 13,
    fontWeight: 600,
  },
  warning: {
    fontSize: 12,
    color: '#e3b341',
  },
  error: {
    fontSize: 12,
    color: '#f85149',
    wordBreak: 'break-all',
  },
};

export default SerialConnect;
