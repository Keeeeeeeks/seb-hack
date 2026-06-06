import { useState } from 'react';
import type { SerialCommand } from './types';

type Props = {
  disabled: boolean;
  onSend: (cmd: SerialCommand) => Promise<void>;
};

export function CommandBox({ disabled, onSend }: Props) {
  const [raw, setRaw] = useState('{"cmd":"selftest"}');
  const [message, setMessage] = useState<string | null>(null);

  async function submit() {
    setMessage(null);
    try {
      await onSend(JSON.parse(raw) as SerialCommand);
      setMessage('sent');
    } catch (err) {
      setMessage(err instanceof Error ? err.message : 'command failed');
    }
  }

  return (
    <section className="panel command-panel" aria-label="Serial command box">
      <div className="panel-heading">
        <div>
          <p className="eyebrow">Board control</p>
          <h2>Command</h2>
        </div>
      </div>
      <textarea
        value={raw}
        onChange={(event) => setRaw(event.target.value)}
        spellCheck={false}
        aria-label="JSON command"
      />
      <div className="command-actions">
        <button type="button" onClick={() => setRaw('{"cmd":"selftest"}')}>selftest</button>
        <button type="button" onClick={() => setRaw('{"cmd":"set","filt_hz":1200}')}>set filter</button>
        <button type="button" disabled={disabled} onClick={() => void submit()}>
          Send to board
        </button>
      </div>
      {message ? <p className="command-message">{message}</p> : null}
    </section>
  );
}
