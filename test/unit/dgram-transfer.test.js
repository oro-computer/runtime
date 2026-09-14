import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { EventEmitter } from 'node:events'
import { randomBytes } from 'node:crypto'
import { promisify } from 'node:util'
import vm from 'node:vm'

const source = readFileSync(new URL('../src/dgram.js', import.meta.url), 'utf8')
const start = source.indexOf("test('client ~> server (~512 messages)'")
const end = source.indexOf("\ntest('connect + disconnect'", start)
assert.ok(start >= 0 && end > start)

function transfer (options = {}) {
  let now = 0
  let nextTimer = 0
  let assertionCount = 0
  const timers = new Map()
  const sockets = []
  const schedule = (callback, delay = 0) => {
    const id = ++nextTimer
    timers.set(id, { callback, time: now + delay })
    return id
  }
  class Socket extends EventEmitter {
    closed = false
    sends = 0
    close (callback) { this.closed = true; callback() }
    address () { return { port: 12345 } }
    bind (port, address, callback) { schedule(callback, 1500) }
    connect (port, address, callback) { schedule(callback, 1500) }
    send (buffer, callback) {
      assert.equal(this.closed, false, 'no sends after socket cleanup')
      this.sends++
      if (options.sendError) {
        schedule(() => callback(new Error('send failed')), 5)
        return
      }
      const delay = options.lateCallback ? 40000 : 10
      schedule(callback, delay)
      if (!options.drop) {
        schedule(() => sockets[0].emit('message', buffer), options.messageFirst ? 5 : 20)
      }
    }
  }
  let pending
  vm.runInNewContext(source.slice(start, end), {
    test: (name, runTest) => { pending = runTest({ ok: value => { assert.ok(value); assertionCount++ } }) },
    process: { env: {} },
    crypto: { randomBytes },
    Buffer,
    util: { promisify },
    dgram: { createSocket: () => { const socket = new Socket(); sockets.push(socket); return socket } },
    setTimeout: schedule,
    clearTimeout: id => timers.delete(id)
  })
  // Observe rejection immediately, including when the fake clock triggers it.
  const result = pending.then(() => null, error => error)
  async function drain () {
    while (timers.size) {
      const [id, timer] = [...timers].sort((a, b) => a[1].time - b[1].time)[0]
      timers.delete(id)
      now = timer.time
      timer.callback()
      await Promise.resolve()
    }
    return result
  }
  return { drain, sockets, assertions: () => assertionCount }
}

test('UDP volume test tolerates slow setup and both receive/send callback orders', async () => {
  for (const messageFirst of [true, false]) {
    const run = transfer({ messageFirst })
    assert.equal(await run.drain(), null)
    assert.equal(run.assertions(), 1)
    assert.equal(run.sockets[1].sends, 512)
    assert.ok(run.sockets.every(socket => socket.closed))
  }
})

test('UDP volume test stops sending after timeout or a send error', async () => {
  for (const options of [{ drop: true }, { lateCallback: true }, { sendError: true }]) {
    const run = transfer(options)
    assert.match((await run.drain()).message, /UDP transfer timed out|send failed/)
    assert.equal(run.assertions(), 0)
    assert.equal(run.sockets[1].sends, 1)
    assert.ok(run.sockets.every(socket => socket.closed))
  }
})
