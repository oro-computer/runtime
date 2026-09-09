import test from 'oro:test'
import qs from 'oro:querystring'

// Matrix-style generation across separators/eq and multiple keys/values.

const combos = [
  ['&', '='],
  [';', '='],
  ['|', ':'],
  ['\t', '='],
  [',', '=']
]

// Generate keys/values
const keys = Array.from({ length: 12 }, (_, i) => `k${i + 1}`)
const vals = Array.from({ length: 12 }, (_, i) => `v${i + 1}`)

// Parsing single pairs across combos
for (let i = 0; i < combos.length; i++) {
  const [sep, eq] = combos[i]
  for (let j = 0; j < keys.length; j++) {
    const k = keys[j]
    const v = vals[j]
    const s = `${k}${eq}${v}`
    test(`qs.parse single ${sep} ${eq} ${k}`, (t) => {
      t.deepEqual(qs.parse(s, sep, eq), { [k]: v })
    })
  }
}
// Parsing repeated keys -> arrays
for (let i = 0; i < combos.length; i++) {
  const [sep, eq] = combos[i]
  for (let j = 0; j < 8; j++) {
    const k = `arr${j + 1}`
    const s = [`${k}${eq}1`, `${k}${eq}2`, `${k}${eq}3`].join(sep)
    test(`qs.parse repeated ${k} using ${sep}${eq}`, (t) => {
      t.deepEqual(qs.parse(s, sep, eq), { [k]: ['1', '2', '3'] })
    })
  }
}

// Stringify objects across combos
for (let i = 0; i < combos.length; i++) {
  const [sep, eq] = combos[i]
  for (let j = 0; j < 10; j++) {
    const obj = { a: '1', b: '2', c: '3' }
    test(`qs.stringify triple ${sep}${eq} #${i}-${j}`, (t) => {
      const out = qs.stringify(obj, sep, eq)
      const parts = out.split(sep)
      t.equal(parts.length, 3)
      t.ok(parts.every((p) => p.includes(eq)))
    })
  }
}
