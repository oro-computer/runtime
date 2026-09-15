import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

const source = readFileSync(new URL('../../api/vm/init.js', import.meta.url), 'utf8')
const world = source.slice(source.indexOf('class World '), source.indexOf('\nclass State '))

function createWorld () {
  const events = new EventTarget()
  const frame = new EventTarget()
  const sent = []
  const timers = new Map()
  frame.contentWindow = { postMessage: (...args) => sent.push(args) }
  const origin = 'https://computer.oro.runtime.tests'
  const context = vm.createContext({
    EventTarget,
    createWorld: () => frame,
    postWindowMessage: (target, ...args) => target.postMessage(...args),
    location: { origin },
    document: { head: { appendChild () {} } },
    addEventListener: events.addEventListener.bind(events),
    removeEventListener: events.removeEventListener.bind(events),
    setTimeout: (callback, delay) => { timers.set(callback, delay); return callback },
    clearTimeout: callback => timers.delete(callback)
  })
  const instance = vm.runInContext(`${world}; new World({}, 'test-world')`, context)
  function ready (source = frame.contentWindow, eventOrigin = origin) {
    const event = new Event('message')
    Object.assign(event, { source, origin: eventOrigin, data: { type: 'world.ready' } })
    events.dispatchEvent(event)
  }
  function advance (elapsed) {
    for (const [callback, delay] of timers) {
      if (delay <= elapsed) callback()
      else timers.set(callback, delay - elapsed)
    }
  }
  return { instance, frame, ready, sent, timers, origin, advance }
}

test('VM evaluation waits for its own iframe handler even when load fires first', async () => {
  const { instance, frame, ready, sent, timers, origin } = createWorld()
  const message = { type: 'script', source: '1 + 2 + 3' }
  const pending = instance.postMessage(message)
  frame.dispatchEvent(new Event('load'))
  await Promise.resolve()
  assert.equal(sent.length, 0)
  ready({})
  ready(frame.contentWindow, 'https://unrelated.example')
  await Promise.resolve()
  assert.equal(sent.length, 0)
  ready()
  await pending
  assert.deepEqual(sent, [[message, origin]])
  assert.equal(timers.size, 0)
})

test('VM window messages use the sender native function without entering the target wrapper', () => {
  const source = readFileSync(new URL('../../api/internal/post-message.js', import.meta.url), 'utf8')
    .replace(/^import .*\n/gm, '')
    .replace('export function postWindowMessage', 'function postWindowMessage')
    .replace('export default null', '')
  const sent = []
  const context = vm.createContext({
    BroadcastChannel: class { postMessage () {} },
    MessagePort: class { postMessage () {} },
    serialize: value => ({ serialized: value }),
    postMessage (message, origin) { sent.push({ target: this, message, origin }) }
  })
  const postWindowMessage = vm.runInContext(`${source}; postWindowMessage`, context)
  const target = { postMessage () { throw new Error('Entered the target realm') } }
  postWindowMessage(target, { type: 'world.ready' }, 'https://computer.oro.runtime.tests')
  assert.equal(sent[0].target, target)
  assert.deepEqual(sent[0].message, { serialized: { type: 'world.ready' } })
  assert.equal(sent[0].origin, 'https://computer.oro.runtime.tests')
})

test('VM iframe startup rejects when JavaScript never becomes ready or loading fails', async () => {
  for (const fail of ['timeout', 'error']) {
    const { instance, frame, timers, advance } = createWorld()
    const rejected = assert.rejects(instance.ready, /VM world test-world/)
    if (fail === 'timeout') {
      frame.dispatchEvent(new Event('load'))
      advance(60_000)
    } else {
      frame.dispatchEvent(new Event('error'))
    }
    await rejected
    assert.equal(timers.size, 0)
  }
})

test('VM frames can finish loading after ten seconds under simulator load', async () => {
  const { instance, ready, sent, advance, timers } = createWorld()
  const message = { type: 'script', source: '42' }
  const pending = instance.postMessage(message)
  advance(15_000)
  ready()
  await pending
  assert.equal(sent.length, 1)
  assert.equal(timers.size, 0)
})

test('VM context windows allow delayed readiness and still reject a missing acknowledgement', async () => {
  const source = readFileSync(new URL('../../api/vm.js', import.meta.url), 'utf8')
  const start = source.indexOf('async function initializeContextWindow ()')
  const end = source.indexOf('\n/**', start)
  for (const ready of [true, false]) {
    const timers = new Map()
    const channel = new EventTarget()
    channel.postMessage = () => {}
    const window = { index: 84, hide: async () => {} }
    const context = vm.createContext({
      contextWindow: null,
      URL,
      VM_WINDOW_PATH: '/oro/vm/index.html',
      VM_WINDOW_TITLE: 'VM',
      location: { origin: 'https://computer.oro.runtime.tests' },
      __args: { config: {} },
      process: { env: {} },
      channel,
      application: { getWindows: async () => [], createWindow: async () => window },
      setTimeout: (callback, delay) => { timers.set(callback, delay); return callback },
      clearTimeout: callback => timers.delete(callback)
    })
    await vm.runInContext(`${source.slice(start, end)}; initializeContextWindow()`, context)
    const pending = ready ? window.ready : assert.rejects(window.ready, /window 84 did not become ready/)
    assert.equal(timers.size, 1)
    for (const [callback, delay] of timers) {
      assert.ok(delay > 15_000 && delay <= 60_000)
      if (!ready) callback()
    }
    if (ready) {
      const event = new Event('message')
      event.data = { ready: 84 }
      channel.dispatchEvent(event)
      assert.equal(timers.size, 0)
    }
    await pending
  }
})
