import test from 'oro:test'
import { Peer, Encryption } from 'oro:latica/index'
import { Packet, PacketQuery } from 'oro:latica/packets'

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

test('unsigned query is dropped when control-plane auth enabled', async (t) => {
  const clusterId = await Encryption.createClusterId('CLUSTER-Q-A')
  const peerId = await Encryption.createId('peer-Q-A')
  const signingKeys = await Encryption.createKeyPair('sig-Q-A')

  const peer = new Peer(
    {
      config: {
        clusterId,
        peerId,
        address: '127.0.0.1',
        signingKeys,
        controlPlaneAuth: 'sig',
        natType: 31,
        keepalive: 1000
      }
    },
    createDgramStub()
  )

  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {},
    usr1: Buffer.from(String(Date.now())),
    usr3: new Uint8Array(32),
    usr4: Buffer.from(String(1))
  }
  const data = await Packet.encode(new PacketQuery(props))
  await peer._onMessage(data, { port: 40001, address: '127.0.0.1' })
  t.equal(peer.metrics.i[PacketQuery.type], 0, 'unsigned query dropped')
})

test('signed query accepted when control-plane auth enabled', async (t) => {
  const clusterId = await Encryption.createClusterId('CLUSTER-Q-B')
  const peerId = await Encryption.createId('peer-Q-B')
  const signingKeys = await Encryption.createKeyPair('sig-Q-B')

  const peer = new Peer(
    {
      config: {
        clusterId,
        peerId,
        address: '127.0.0.1',
        signingKeys,
        controlPlaneAuth: 'sig',
        natType: 31,
        keepalive: 1000
      }
    },
    createDgramStub()
  )

  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {},
    usr1: Buffer.from(String(Date.now())),
    usr3: new Uint8Array(32),
    usr4: Buffer.from(String(1))
  }
  peer._applyControlAuth(PacketQuery, props)
  const data = await Packet.encode(new PacketQuery(props))
  await peer._onMessage(data, { port: 40002, address: '127.0.0.1' })
  t.equal(peer.metrics.i[PacketQuery.type], 1, 'signed query accepted')
})
