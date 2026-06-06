/**
 * CommandBox.jsx
 *
 * Free-text JSON input + Send button. Sends line over serial WritableStream.
 * Preset buttons for common commands.
 *
 * Props:
 *   writer — SerialPort WritableStreamDefaultWriter (or null if disconnected)
 */
import React, { useState, useCallback } from 'react';

const PRESETS = [
  { label: 'Selftest',      payload: { cmd: 'selftest' } },
  { label: 'Filter 800 Hz', payload: { cmd: 'set', filt_hz: 800 } },
  { label: 'Filter 3000 Hz',payload: { cmd: 'set', filt_hz: 3000 } },
];

const ENCODER = new TextEncoder();

async function sendLine(writer, text) {
  if (!writer) return;
  const line = text.endsWith('\n') ? text : text + '\n';
  await writer.write(ENCODER.encode(line));
}

export function CommandBox({ writer }) {
  const [input, setInput]   = useState('');
  const [status, setStatus] = useState(null); // { ok: bool, msg: string }

  const send = useCallback(async (jsonStr) => {
    setStatus(null);
    // Validate JSON before sending
    try {
      JSON.parse(jsonStr);
    } catch (_e) {
      setStatus({ ok: false, msg: 'Invalid JSON' });
      return;
    }
    if (!writer) {
      setStatus({ ok: false, msg: 'Not connected' });
      return;
    }
    try {
      await sendLine(writer, jsonStr);
      setStatus({ ok: true, msg: 'Sent ✓' });
    } catch (err) {
      setStatus({ ok: false, msg: err.message || 'Send failed' });
    }
  }, [writer]);

  const handleSend = useCallback(() => {
    const trimmed = input.trim();
    if (!trimmed) return;
    send(trimmed);
  }, [input, send]);

  const handlePreset = useCallback((payload) => {
    const str = JSON.stringify(payload);
    setInput(str);
    send(str);
  }, [send]);

  const handleKeyDown = useCallback((e) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      handleSend();
    }
  }, [handleSend]);

  return (
    <div style={styles.container}>
      <div style={styles.header}>Command Box</div>

      {/* Preset buttons */}
      <div style={styles.presetRow}>
        {PRESETS.map((p) => (
          <button
            key={p.label}
            style={styles.presetBtn}
            onClick={() => handlePreset(p.payload)}
            disabled={!writer}
            title={JSON.stringify(p.payload)}
          >
            {p.label}
          </button>
        ))}
      </div>

      {/* Free-text input */}
      <div style={styles.inputRow}>
        <input
          type="text"
          value={input}
          onChange={(e) => setInput(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder='{"cmd":"set","filt_hz":1200}'
          style={styles.input}
          spellCheck={false}
          disabled={!writer}
        />
        <button
          style={styles.sendBtn}
          onClick={handleSend}
          disabled={!writer || !input.trim()}
        >
          Send
        </button>
      </div>

      {/* Status feedback */}
      {status && (
        <div style={{ ...styles.statusMsg, color: status.ok ? '#3fb950' : '#f85149' }}>
          {status.msg}
        </div>
      )}

      {!writer && (
        <div style={styles.hint}>Connect serial port to enable commands.</div>
      )}
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
    gap: 10,
  },
  header: {
    fontWeight: 700,
    fontSize: 13,
    color: '#8b949e',
    textTransform: 'uppercase',
    letterSpacing: '0.05em',
  },
  presetRow: {
    display: 'flex',
    gap: 6,
    flexWrap: 'wrap',
  },
  presetBtn: {
    background: '#21262d',
    color: '#e6edf3',
    border: '1px solid #30363d',
    borderRadius: 6,
    padding: '5px 12px',
    cursor: 'pointer',
    fontSize: 12,
    fontWeight: 500,
    transition: 'background 0.15s',
  },
  inputRow: {
    display: 'flex',
    gap: 6,
  },
  input: {
    flex: 1,
    background: '#0d1117',
    color: '#e6edf3',
    border: '1px solid #30363d',
    borderRadius: 6,
    padding: '6px 10px',
    fontSize: 13,
    fontFamily: 'monospace',
    outline: 'none',
  },
  sendBtn: {
    background: '#238636',
    color: '#fff',
    border: 'none',
    borderRadius: 6,
    padding: '6px 16px',
    cursor: 'pointer',
    fontSize: 13,
    fontWeight: 600,
  },
  statusMsg: {
    fontSize: 12,
    fontWeight: 600,
  },
  hint: {
    fontSize: 11,
    color: '#8b949e',
  },
};

export default CommandBox;
