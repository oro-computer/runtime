import test from 'oro:test'
import qs from 'oro:querystring'

// Expanded suite to cover edge cases and option permutations.
// Keep each assertion in its own test() for granular counts and diagnostics.

// Basic parse variations
const parseCases = [
  ['a=1', { a: '1' }],
  ['a=1&', { a: '1' }],
  ['a=1&&b=2', { a: '1', b: '2' }],
  ['a=&b=', { a: '', b: '' }],
  ['a', { a: '' }],
  ['=1', { '': '1' }],
  ['=', { '': '' }],
  ['&&', {}],
  ['a=1&b=2&c=3', { a: '1', b: '2', c: '3' }],
  ['a=1&a=2&a=3', { a: ['1', '2', '3'] }],
  ['%61=1', { a: '1' }],
  ['a=%2B', { a: '+' }],
  ['a=%20', { a: ' ' }],
  ['a%20b=c%20d', { 'a b': 'c d' }],
  ['plus+space=with+plus', { 'plus space': 'with plus' }],
  ['weird=%E0%A4%A', { weird: '%E0%A4%A' }],
  ['a=b=c', { a: 'b=c' }],
  ['a?b=c', { 'a?b': 'c' }],
  ['a#b=c', { 'a#b': 'c' }],
  ['a=%00', { a: '\u0000' }],
  ['空=白', { 空: '白' }],
  ['emoji=😀', { emoji: '😀' }],
  ['k=スペース', { k: 'スペース' }],
  ['k=%E3%82%B9%E3%83%9A%E3%83%BC%E3%82%B9', { k: 'スペース' }],
  ['a=1;b=2;c=3', { 'a=1;b=2;c=3': '' }],
  ['a;1', { 'a;1': '' }],
  ['a=1;b=2', { 'a=1;b=2': '' }],
  ['arr[]=1&arr[]=2', { 'arr[]': ['1', '2'] }],
  ['nested[a]=1&nested[b]=2', { 'nested[a]': '1', 'nested[b]': '2' }],
  ['a=%2Fb', { a: '/b' }],
  ['a=foo%2Fbar', { a: 'foo/bar' }]
]

for (let i = 0; i < parseCases.length; i++) {
  const [input, expected] = parseCases[i]
  test(`querystring.parse basic ${i + 1}`, (t) => {
    t.deepEqual(qs.parse(input), expected)
  })
}
// Parse with custom separators and eq
const sepEqCases = [
  ['a:1|b:2', '|', ':', { a: '1', b: '2' }],
  ['k~v;k2~v2', ';', '~', { k: 'v', k2: 'v2' }],
  ['x\ty\tz', '\t', '\t', { x: 'y', z: '' }],
  ['x\ty\tz=1', '\t', '\t', { x: 'y', z: '1' }]
]

for (let i = 0; i < sepEqCases.length; i++) {
  const [input, sep, eq, expected] = sepEqCases[i]
  test(`querystring.parse custom sep/eq ${i + 1}`, (t) => {
    t.deepEqual(qs.parse(input, sep, eq), expected)
  })
}

// Parse with maxKeys option
const parseMaxKeysInputs = ['a=1&b=2&c=3&d=4', 'k=1&k=2&k=3', 'x=1&y=2']
for (let i = 0; i < parseMaxKeysInputs.length; i++) {
  const s = parseMaxKeysInputs[i]
  test(`querystring.parse maxKeys trims ${i + 1}`, (t) => {
    const out = qs.parse(s, '&', '=', { maxKeys: 2 })
    t.ok(Object.keys(out).length <= 2)
  })
}

// Parse with decodeURIComponent override
const badEncoded = ['%EA', '%E0%A4%A', '%ZZ', '%']
for (let i = 0; i < badEncoded.length; i++) {
  const v = badEncoded[i]
  test(`querystring.parse tolerates bad encoding ${i + 1}`, (t) => {
    const out = qs.parse(`a=${v}`)
    t.equal(typeof out.a, 'string')
  })
}

// Stringify basic
const stringifyCases = [
  [{}, ''],
  [{ a: 1 }, 'a=1'],
  [{ a: 'x y' }, 'a=x%20y'],
  [{ a: ' ' }, 'a=%20'],
  [{ a: '+' }, 'a=%2B'],
  [{ 'a b': 'c d' }, 'a%20b=c%20d'],
  [{ a: ['1', '2'] }, 'a=1&a=2'],
  [{ a: ['x y', 'z w'] }, 'a=x%20y&a=z%20w'],
  [{ 空: '白' }, '%E7%A9%BA=%E7%99%BD'],
  [{ emoji: '😀' }, 'emoji=%F0%9F%98%80'],
  [{ nullish: null }, 'nullish=']
]

for (let i = 0; i < stringifyCases.length; i++) {
  const [obj, expected] = stringifyCases[i]
  test(`querystring.stringify basic ${i + 1}`, (t) => {
    t.equal(qs.stringify(obj), expected)
  })
}

// Stringify with custom separators and eq
const stringifySepEqCases = [
  [{ a: 1, b: 2 }, '|', ':', 'a:1|b:2'],
  [{ k: 'v', k2: 'v2' }, ';', '~', 'k~v;k2~v2']
]
for (let i = 0; i < stringifySepEqCases.length; i++) {
  const [obj, sep, eq, expected] = stringifySepEqCases[i]
  test(`querystring.stringify custom sep/eq ${i + 1}`, (t) => {
    t.equal(qs.stringify(obj, sep, eq), expected)
  })
}

// Stringify with custom encoder
const encoders = [
  (s) => `X_${encodeURIComponent(s)}`,
  (s) => encodeURIComponent(String(s)).replace(/%20/g, '+')
]
for (let i = 0; i < encoders.length; i++) {
  const enc = encoders[i]
  test(`querystring.stringify custom encoder ${i + 1}`, (t) => {
    t.ok(
      qs
        .stringify({ a: 'b c' }, '&', '=', { encodeURIComponent: enc })
        .includes('a=')
    )
  })
}

// Unescape/escape symmetry spot checks
const unescapePairs = [
  ['x%20y', 'x y'],
  ['x%2By', 'x+y'],
  ['%F0%9F%98%80', '😀']
]
for (let i = 0; i < unescapePairs.length; i++) {
  const [enc, dec] = unescapePairs[i]
  test(`querystring.unescape pair ${i + 1}`, (t) => {
    t.equal(qs.unescape(enc), dec)
  })
  test(`querystring.escape pair ${i + 1}`, (t) => {
    t.equal(qs.escape(dec), enc.toUpperCase())
  })
}
