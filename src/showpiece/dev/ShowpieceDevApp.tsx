import { TempDetune } from '../TempDetune'
import { IfField } from '../telemetry/IfField'
import { useFixtureSource } from '../telemetry/useFixtureSource'
import { fixtureFrames } from './fixture'

export function ShowpieceDevApp() {
  const frame = useFixtureSource(fixtureFrames, 30)

  return (
    <main style={{ minHeight: '100vh', background: '#020617', display: 'grid', placeItems: 'center', padding: 24 }}>
      <IfField frame={frame} field="detune_c">
        {frame && <TempDetune frame={frame} />}
      </IfField>
    </main>
  )
}
