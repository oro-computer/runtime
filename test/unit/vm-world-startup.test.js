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
  const timers = new Set()
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
    setTimeout: callback => { timers.add(callback); return callback },
    clearTimeout: callback => timers.delete(callback)
  })
  const instance = vm.runInContext(`${world}; new World({}, 'test-world')`, context)
  function ready (source = frame.contentWindow, eventOrigin = origin) {
    const event = new Event('message')
    Object.assign(event, { source, origin: eventOrigin, data: { type: 'world.ready' } })
    events.dispatchEvent(event)
  }
  return { instance, frame, ready, sent, timers, origin }
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
    const { instance, frame, timers } = createWorld()
    const rejected = assert.rejects(instance.ready, /VM world test-world/)
    if (fail === 'timeout') {
      frame.dispatchEvent(new Event('load'))
      for (const callback of timers) callback()
    } else {
      frame.dispatchEvent(new Event('error'))
    }
    await rejected
    assert.equal(timers.size, 0)
  }
})
