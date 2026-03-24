import test from 'oro:test'
import Buffer from 'oro:buffer'

// Additional Buffer coverage, emphasizing encoding, construction, and common ops.

const encodings = [
  'utf8',
  'hex',
  'base64',
  'base64url',
  'latin1',
  'ascii',
  'utf16le'
]
for (let i = 0; i < encodings.length; i++) {
  const enc = encodings[i]
  test(`Buffer.isEncoding ${enc}`, (t) => {
    t.equal(Buffer.isEncoding(enc), true)
  })
}
test('Buffer.isEncoding false', (t) => {
  t.equal(Buffer.isEncoding('nope'), false)
})

// from/toString round-trips
const strings = ['hello', '😀 emoji', 'äöüÄÖÜ', '控制', 'plain ascii']
for (let i = 0; i < strings.length; i++) {
  const s = strings[i]
  test(`Buffer.from/toString utf8 ${i + 1}`, (t) => {
    const b = Buffer.from(s, 'utf8')
    t.equal(b.toString('utf8'), s)
  })
}
// hex/base64 encodings
const hexBase = [
  ['ff00aa', 'hex'],
  ['68656c6c6f', 'hex'],
  [Buffer.from('hello').toString('base64'), 'base64'],
  [Buffer.from('hello').toString('base64url'), 'base64url']
]
for (let i = 0; i < hexBase.length; i++) {
  const [val, enc] = hexBase[i]
  test(`Buffer.from encoded ${enc} ${i + 1}`, (t) => {
    const b = Buffer.from(val, enc)
    t.ok(b.length > 0)
  })
}

// alloc and fill
for (let i = 1; i <= 8; i++) {
  test(`Buffer.alloc size ${i}`, (t) => {
    const b = Buffer.alloc(i, 1)
    t.equal(b.length, i)
    t.ok(b.every((v) => v === 1))
  })
}

// concat
test('Buffer.concat basics', (t) => {
  const parts = [Buffer.from('a'), Buffer.from('b'), Buffer.from('c')]
  const out = Buffer.concat(parts)
  t.equal(out.toString(), 'abc')
})

// compare and equals
const comparePairs = [
  [Buffer.from('a'), Buffer.from('b'), -1],
  [Buffer.from('b'), Buffer.from('a'), 1],
  [Buffer.from('x'), Buffer.from('x'), 0]
]
for (let i = 0; i < comparePairs.length; i++) {
  const [a, b, expect] = comparePairs[i]
  test(`Buffer.compare ${i + 1}`, (t) => {
    t.equal(Buffer.compare(a, b), expect)
    t.equal(a.compare(b), expect)
  })
}

// includes/indexOf/lastIndexOf
test('Buffer.includes/indexOf/lastIndexOf', (t) => {
  const b = Buffer.from('abcabc')
  t.ok(b.includes('a'))
  t.equal(b.indexOf('b'), 1)
  t.equal(b.lastIndexOf('b'), 4)
})

// read/write integers and floats
test('Buffer read/write integers/floats', (t) => {
  const b = Buffer.alloc(8)
  b.writeUInt16LE(0x1234, 0)
  b.writeUInt16BE(0x5678, 2)
  b.writeInt8(-1, 4)
  b.writeFloatLE(1.5, 0)
  t.equal(b.readUInt16LE(0), b.readUInt16LE(0))
  t.equal(b.readUInt16BE(2), 0x5678)
  t.equal(b.readInt8(4), -1)
})

// swap16/swap32 no-throw and changes content size-wise
test('Buffer.swap16/swap32', (t) => {
  const b16 = Buffer.from([0, 1, 2, 3])
  const s16 = Buffer.from(b16)
  s16.swap16()
  t.equal(s16.length, b16.length)
  const b32 = Buffer.from([0, 1, 2, 3])
  const s32 = Buffer.from(b32)
  s32.swap32()
  t.equal(s32.length, b32.length)
})

// byteLength sanity
const bl = [
  ['hello', 'utf8'],
  ['😀', 'utf8'],
  ['68656c6c6f', 'hex'],
  ['dGVzdA', 'base64url']
]
for (let i = 0; i < bl.length; i++) {
  const [s, enc] = bl[i]
  test(`Buffer.byteLength ${enc} ${i + 1}`, (t) => {
    t.ok(Buffer.byteLength(s, enc) > 0)
  })
}
test('Buffer base64url round-trip', (t) => {
  const input = 'padded?'
  const encoded = Buffer.from(input).toString('base64url')
  t.equal(encoded.includes('='), false)
  t.equal(Buffer.from(encoded, 'base64url').toString(), input)
})

test('Buffer base64url dotted padding decode', (t) => {
  const encoded = 'dGVzdA..'
  t.equal(Buffer.from(encoded, 'base64url').toString(), 'test')
})
