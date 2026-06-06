import type { ReactNode } from 'react';
import type { TelemetryFrame } from './types';

type Props<K extends keyof TelemetryFrame> = {
  frame: TelemetryFrame | null;
  field: K;
  children: (value: NonNullable<TelemetryFrame[K]>) => ReactNode;
};

export function IfField<K extends keyof TelemetryFrame>({ frame, field, children }: Props<K>) {
  const value = frame?.[field];
  if (value == null) return null;
  return <>{children(value as NonNullable<TelemetryFrame[K]>)}</>;
}
