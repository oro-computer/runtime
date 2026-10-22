import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

const source = readFileSync(new URL('../../api/test/index.js', import.meta.url), 'utf8')
const start = source.indexOf('  throws (fn, expected, message) {')
const end = source.indexOf('\n  /**', start)
const checks = []
const harness = vm.runInNewContext(`({ ${source.slice(start, end)} })`)
harness._assert = pass => checks.push(pass)

test('Oro throws assertions match error constructors and reject different or missing exceptions', () => {
  harness.throws(() => { throw new TypeError('public validation') }, TypeError)
  assert.equal(checks.at(-1), true)
  harness.throws(() => { throw new RangeError('public bounds') }, TypeError)
  assert.equal(checks.at(-1), false)
  harness.throws(() => {}, TypeError)
  assert.equal(checks.at(-1), false)
  class PublicError extends Error {}
  harness.throws(() => { throw new PublicError() }, PublicError)
  assert.equal(checks.at(-1), true)
  harness.throws(() => { throw new TypeError() }, Error)
  assert.equal(checks.at(-1), true)
  assert.throws(() => harness.throws(() => { throw new Error() }, () => true), /not implemented/)
})
