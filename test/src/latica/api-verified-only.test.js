import test from 'oro:test'
import api from 'oro:latica/api'
import { Encryption } from 'oro:latica/index'

function createDgramStub () {
  class StubSocket {
    constructor () {
      this._events = {}
    }

    on (ev, fn) {
      this._events[ev] = fn
    }

    removeAllListeners () {
      this._events = {}
    }

    setMaxListeners () {}
    bind () {
      if (this._events.listening) this._events.listening()
    }

    address () {
      return { port: 12345 }
    }

    send () {}
    close () {}
    unref () {
      return this
    }
  }
  return {
    createSocket () {
      return new StubSocket()
    }
  }
}

class Emitter {
  constructor () {
    this.map = new Map()
  }

  on (name, fn) {
    ;(this.map.get(name) || this.map.set(name, []).get(name)).push(fn)
    return this
  }

  once (name, fn) {
    const wrap = (...a) => {
      this.off(name, wrap)
      fn(...a)
    }
    return this.on(name, wrap)
  }

  off (name, fn) {
    const arr = this.map.get(name) || []
    this.map.set(
      name,
      arr.filter((f) => f !== fn)
    )
    return this
  }

  emit (name, ...args) {
    for (const fn of this.map.get(name) || []) fn(...args)
  }

  removeListener (name, fn) {
    return this.off(name, fn)
  }
}

// API-level emit/compose path

test('bus composes large publish and delivers opened plaintext', async (t) => {
  const clusterId = await Encryption.createClusterId('API-COMPOSE')
  const peerId = await Encryption.createId('peer-API-COMPOSE')
  const signingKeys = await Encryption.createKeyPair('sig-API-COMPOSE')
  const sharedKey = await Encryption.createSharedKey('sub-B')

  const bus = await api(
    { clusterId, peerId, signingKeys, natType: 31 },
    { EventEmitter: Emitter },
    createDgramStub()
  )
  const sub = await bus.subcluster({ sharedKey })

  const payload = Buffer.alloc(4096, 0x61) // 4 KiB of 'a'
  const ev = 'big'

  let received = 0
  await sub.on(ev, (opened /* Buffer */) => {
    received = opened.length
  })

  const packets = await sub.emit(ev, payload)
  t.ok(Array.isArray(packets), 'emit returns packets')
  t.equal(received, payload.length, 'composed plaintext delivered')
})
