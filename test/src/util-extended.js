import { test } from 'oro:test'
import util, {
  hasOwnProperty,
  isNumber,
  isBoolean,
  isSymbol,
  isError,
  isArrayBuffer,
  isArrayBufferView,
  isRegExp,
  isClass,
  isPromiseLike,
  toBuffer,
  clamp,
  promisify,
  toProperCase,
  splitBuffer
} from 'oro:util'
import Buffer from 'oro:buffer'

test('util.isNumber primitives and objects', (t) => {
  t.ok(isNumber(0))
  t.ok(isNumber(NaN))
  // Wrapper object (avoid new Number per lint rules)
  t.ok(isNumber(Object(1)))
})

test('util.isBoolean primitives and objects', (t) => {
  t.ok(isBoolean(true))
  t.ok(isBoolean(false))
  // Wrapper object (avoid new Boolean per lint rules)
  t.ok(isBoolean(Object(false)))
})

test('util.isSymbol', (t) => {
  t.ok(isSymbol(Symbol('x')))
  t.ok(!isSymbol('not'))
})

test('util.isError basic', (t) => {
  t.ok(isError(new Error('x')))
  t.ok(!isError('x'))
})

test('util.isArrayBuffer and view', (t) => {
  const ab = new ArrayBuffer(8)
  const view = new Uint8Array(ab)
  t.ok(isArrayBuffer(ab))
  t.ok(isArrayBufferView(view))
})

test('util.isRegExp', (t) => {
  t.ok(isRegExp(/x/))
  t.ok(!isRegExp('x'))
})

test('util.isClass vs function', (t) => {
  class X {}
  function fn () {}
  t.ok(isClass(X))
  t.ok(!isClass(fn))
})

test('util.isPromiseLike', (t) => {
  // oxlint-disable-next-line unicorn/no-thenable
  const p = { then: () => {} }
  t.ok(isPromiseLike(Promise.resolve(1)))
  t.ok(isPromiseLike(p))
  // oxlint-disable-next-line unicorn/no-thenable
  t.ok(!isPromiseLike({ then: 1 }))
})

test('util.hasOwnProperty supports symbol', (t) => {
  const s = Symbol('a')
  const o = { [s]: 1 }
  t.ok(hasOwnProperty(o, s))
})

test('util.toBuffer from string and typed array', (t) => {
  const b1 = toBuffer('abc')
  t.ok(Buffer.isBuffer(b1))
  const b2 = toBuffer(new Uint8Array([1, 2, 3]))
  t.ok(Buffer.isBuffer(b2))
})

test('util.clamp non-finite coerces to min', (t) => {
  t.equal(clamp(NaN, 0, 10), 0)
  t.equal(clamp(Infinity, 0, 5), 5)
})

test('util.promisify simple callback', async (t) => {
  const cb = (x, y, done) => done(null, x + y)
  const p = promisify(cb)
  const v = await p(1, 2)
  t.equal(v, 3)
})

test('util.promisify error path', async (t) => {
  const cb = (done) => done(new Error('boom'))
  const p = promisify(cb)
  try {
    await p()
    t.fail('should throw')
  } catch (err) {
    t.ok(/boom/.test(String(err)))
  }
})

test('util.toProperCase empty and strings', (t) => {
  t.equal(toProperCase(''), '')
  t.equal(toProperCase('x'), 'X')
  t.equal(toProperCase('hello world'), 'Hello world')
})

// splitBuffer chunking for various boundaries
const payload = Buffer.from('abcdefghijklmnopqrstuvwxyz')
const hwm = [1, 2, 3, 5, 7, 8]
for (let i = 0; i < hwm.length; i++) {
  const n = hwm[i]
  test(`util.splitBuffer chunk size ${n}`, (t) => {
    const parts = splitBuffer(payload, n)
    t.ok(parts.length >= Math.ceil(payload.length / n))
    t.equal(Buffer.concat(parts).toString(), payload.toString())
  })
}

// util.debug existence and toggling enabled flag (environment-independent)
test('util.debug toggling enabled flag', (t) => {
  const log = util.debug('nonexistent-section')
  t.equal(typeof log, 'function')
  const prev = log.enabled
  log.enabled = !prev
  t.equal(log.enabled, !prev)
  log.enabled = prev
  t.equal(log.enabled, prev)
})
