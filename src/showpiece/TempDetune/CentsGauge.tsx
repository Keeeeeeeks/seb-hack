const LIMIT = 75
const ARC_DEG = 220
const CX = 90
const CY = 86
const R = 62

type CentsGaugeProps = {
  cents: number
}

function clamp(n: number, lo: number, hi: number) {
  return Math.min(hi, Math.max(lo, n))
}

function polar(deg: number, radius = R) {
  const rad = (deg * Math.PI) / 180
  return { x: CX + radius * Math.cos(rad), y: CY + radius * Math.sin(rad) }
}

function arcPath(startDeg: number, endDeg: number) {
  const start = polar(startDeg)
  const end = polar(endDeg)
  const largeArc = Math.abs(endDeg - startDeg) > 180 ? 1 : 0
  return `M ${start.x.toFixed(2)} ${start.y.toFixed(2)} A ${R} ${R} 0 ${largeArc} 1 ${end.x.toFixed(2)} ${end.y.toFixed(2)}`
}

export function CentsGauge({ cents }: CentsGaugeProps) {
  const shown = clamp(cents, -LIMIT, LIMIT)
  const start = 160
  const end = start + ARC_DEG
  const zeroDeg = start + ARC_DEG / 2
  const needleDeg = start + ((shown + LIMIT) / (LIMIT * 2)) * ARC_DEG
  const needle = polar(needleDeg, R - 14)
  const zeroOuter = polar(zeroDeg, R + 1)
  const zeroInner = polar(zeroDeg, R - 13)
  const label = `Temperature detune ${shown.toFixed(1)} cents`

  return (
    <svg className="temp-detune-gauge" viewBox="0 0 180 132" role="img" aria-label={label}>
      <path d={arcPath(start, end)} fill="none" stroke="rgba(148, 163, 184, 0.28)" strokeWidth="10" strokeLinecap="round" />
      <path d={arcPath(start, needleDeg)} fill="none" stroke="#38bdf8" strokeWidth="10" strokeLinecap="round" />
      <line x1={zeroInner.x} y1={zeroInner.y} x2={zeroOuter.x} y2={zeroOuter.y} stroke="#f8fafc" strokeWidth="3" strokeLinecap="round" />
      <line x1={CX} y1={CY} x2={needle.x} y2={needle.y} stroke="#fb923c" strokeWidth="3.5" strokeLinecap="round" />
      <circle cx={CX} cy={CY} r="5" fill="#fb923c" />
      <text x={CX} y="116" textAnchor="middle" fontSize="20" fontWeight="700" fill="#f8fafc" style={{ fontVariantNumeric: 'tabular-nums' }}>
        {shown.toFixed(1)}¢
      </text>
      <text x="24" y="126" fontSize="10" fill="#94a3b8">-75¢</text>
      <text x="132" y="126" fontSize="10" fill="#94a3b8">+75¢</text>
    </svg>
  )
}
