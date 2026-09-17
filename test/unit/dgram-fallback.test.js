import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { AsyncResource } from 'node:async_hooks'
import { EventEmitter } from 'node:events'
import { isIPv4 } from 'node:net'
import vm from 'node:vm'

const source = readFileSync(new URL('../../api/dgram.js', import.meta.url), 'utf8')
  .replace(/^import .*$/gm, '')
  .replace(/^export default exports$/m, '')
  .replace(/^export /gm, '')

function createRuntime () {
  const events = new EventTarget()
  const listeners = new Set()
  class Conduit extends EventTarget {
    isActive = true
    receive (callback) { this.onreceive = callback }
    send () { return this.isActive }
    reconnect () { return Promise.resolve(this) }
    close () { this.isActive = false }
  }
  const result = { data: { bound: true, size: 65536 } }
  const Socket = vm.runInNewContext(`${source}\nSocket`, {
    EventEmitter,
    AsyncResource,
    Conduit,
    Buffer,
    InternalError: Error,
    isIPv4,
    isFunction: value => typeof value === 'function',
    noop () {},
    rand64: () => 1n,
    gc: { finalizer: Symbol('finalizer'), ref () {}, unref () {} },
    diagnostics: { channels: { group: () => ({ channel: () => ({ publish () {} }) }) } },
    ipc: { request: async () => result, send: async () => result, sendSync: () => result },
    setTimeout,
    clearTimeout,
    addEventListener (name, listener) {
      if (name === 'data') listeners.add(listener)
      events.addEventListener(name, listener)
    },
    removeEventListener (name, listener) {
      if (name === 'data') listeners.delete(listener)
      events.removeEventListener(name, listener)
    }
  })
  const socket = new Socket('udp4')
  function deliver (message, id = socket.id) {
    const event = new Event('data')
    event.detail = {
      params: { source: 'udp.readStart', data: { id, address: '127.0.0.1', port: 41251 } },
      data: Buffer.from(message)
    }
    events.dispatchEvent(event)
  }
  return { socket, listeners, deliver }
}

test('UDP receives IPC datagrams during Conduit restart and after reopen', async () => {
  const { socket, listeners, deliver } = createRuntime()
  const received = []
  socket.on('message', (buffer, info) => received.push([buffer.toString(), info.port]))
  await new Promise((resolve, reject) => {
    socket.bind(0, '127.0.0.1', error => error ? reject(error) : resolve())
  })
  try {
    socket.conduit.onreceive(null, {
      options: { address: '127.0.0.1', port: '41251' },
      payload: Buffer.from('before')
    })
    // Native delivery can fall back before the WebSocket close event arrives.
    deliver('during')
    socket.conduit.isActive = false
    socket.conduit.dispatchEvent(new Event('close'))
    deliver('disconnected')
    socket.conduit.isActive = true
    socket.conduit.dispatchEvent(new Event('reopen'))
    // An IPC response already queued before reopen must still be consumed.
    deliver('queued')
    deliver('another socket', '2')
    assert.deepEqual(received, [
      ['before', 41251], ['during', 41251], ['disconnected', 41251], ['queued', 41251]
    ])
    assert.equal(listeners.size, 1, 'reopen does not duplicate the fallback listener')
  } finally {
    await new Promise((resolve, reject) => socket.close(error => error ? reject(error) : resolve()))
  }
  assert.equal(listeners.size, 0, 'close removes the fallback listener')
  deliver('after close')
  assert.equal(received.length, 4)
})
