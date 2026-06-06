import type { ReactNode } from 'react'
import type { TelemetryFrame } from './types'

type Props<K extends keyof TelemetryFrame> = {
  frame: TelemetryFrame | null
  field: K
  children: ReactNode
}

export function IfField<K extends keyof TelemetryFrame>({ frame, field, children }: Props<K>) {
  if (!frame || !(field in frame)) return null
  return <>{children}</>
}
