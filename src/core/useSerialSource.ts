import { useCallback, useEffect, useRef, useState } from 'react';
import type { SerialCommand, TelemetryFrame } from './types';

class LineBreakTransformer implements Transformer<string, string> {
  private buffer = '';

  transform(chunk: string, controller: TransformStreamDefaultController<string>) {
    this.buffer += chunk;
    const lines = this.buffer.split('\n');
    this.buffer = lines.pop() ?? '';

    for (const line of lines) {
      const trimmed = line.trim();
      if (trimmed.length > 0) controller.enqueue(trimmed);
    }
  }

  flush(controller: TransformStreamDefaultController<string>) {
    const trimmed = this.buffer.trim();
    if (trimmed.length > 0) controller.enqueue(trimmed);
  }
}

type SerialState = {
  connected: boolean;
  connecting: boolean;
  error: string | null;
  supported: boolean;
};

export function useSerialSource(onFrame: (frame: TelemetryFrame) => void) {
  const portRef = useRef<SerialPort | null>(null);
  const readerRef = useRef<ReadableStreamDefaultReader<string> | null>(null);
  const closedRef = useRef<Promise<void> | null>(null);
  const onFrameRef = useRef(onFrame);
  const [state, setState] = useState<SerialState>({
    connected: false,
    connecting: false,
    error: null,
    supported: typeof navigator !== 'undefined' && 'serial' in navigator,
  });

  useEffect(() => {
    onFrameRef.current = onFrame;
  }, [onFrame]);

  const disconnect = useCallback(async () => {
    const reader = readerRef.current;
    readerRef.current = null;
    if (reader) await reader.cancel().catch(() => undefined);
    if (closedRef.current) await closedRef.current.catch(() => undefined);
    closedRef.current = null;

    const port = portRef.current;
    portRef.current = null;
    if (port) await port.close().catch(() => undefined);
    setState((prev) => ({ ...prev, connected: false, connecting: false }));
  }, []);

  const connect = useCallback(async () => {
    if (!navigator.serial) {
      setState((prev) => ({ ...prev, error: 'Web Serial requires Chrome/Edge on localhost or HTTPS.' }));
      return;
    }

    setState((prev) => ({ ...prev, connecting: true, error: null }));
    try {
      const port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x0483 }] });
      await port.open({ baudRate: 115200 });
      portRef.current = port;

      if (!port.readable) throw new Error('Serial port opened without a readable stream.');

      const decoder = new TextDecoderStream();
      closedRef.current = port.readable.pipeTo(decoder.writable);
      const reader = decoder.readable
        .pipeThrough(new TransformStream(new LineBreakTransformer()))
        .getReader();
      readerRef.current = reader;
      setState((prev) => ({ ...prev, connected: true, connecting: false }));

      void (async () => {
        try {
          while (true) {
            const { value, done } = await reader.read();
            if (done) break;
            try {
              onFrameRef.current(JSON.parse(value) as TelemetryFrame);
            } catch {
              // Drop malformed lines; firmware keeps streaming.
            }
          }
        } catch (err) {
          setState((prev) => ({ ...prev, error: err instanceof Error ? err.message : 'Serial read failed.' }));
        } finally {
          await disconnect();
        }
      })();
    } catch (err) {
      setState((prev) => ({
        ...prev,
        connected: false,
        connecting: false,
        error: err instanceof Error ? err.message : 'Serial connection failed.',
      }));
    }
  }, [disconnect]);

  const sendCommand = useCallback(async (cmd: SerialCommand) => {
    const port = portRef.current;
    if (!port?.writable) throw new Error('Serial port is not connected.');
    const writer = port.writable.getWriter();
    try {
      await writer.write(new TextEncoder().encode(`${JSON.stringify(cmd)}\n`));
    } finally {
      writer.releaseLock();
    }
  }, []);

  useEffect(() => {
    const serial = navigator.serial;
    if (!serial) return;
    const onDisconnect = () => {
      void disconnect();
    };
    serial.addEventListener('disconnect', onDisconnect);
    return () => {
      serial.removeEventListener('disconnect', onDisconnect);
      void disconnect();
    };
  }, [disconnect]);

  return { ...state, connect, disconnect, sendCommand };
}
