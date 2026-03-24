import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'

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

test('returnRoutes entries are pruned on mainLoop', async (t) => {
  const clusterId = await Encryption.createClusterId('Q-RR')
  const peerId = await Encryption.createId('peer-QR')
  const signingKeys = await Encryption.createKeyPair('sig-QR')
  const peer = new Peer(
    {
      config: {
        clusterId,
        peerId,
        address: '127.0.0.1',
        signingKeys,
        natType: NAT.UNRESTRICTED,
        keepalive: 1000
      }
    },
    createDgramStub()
  )

  const sizeBefore = peer.returnRoutes.size
  await peer.query('hello?')
  const sizeAfterQuery = peer.returnRoutes.size
  t.ok(sizeAfterQuery > sizeBefore, 'returnRoutes added entry')

  // Run main loop once, which decrements and prunes falsy entries
  await peer._mainLoop(Date.now())
  t.equal(peer.returnRoutes.size, 0, 'returnRoutes cleared by main loop')
})
