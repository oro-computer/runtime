import test from 'oro:test'
import { Peer, Encryption } from 'oro:latica/index'
import { Packet, PacketPong } from 'oro:latica/packets'

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

test('unsigned pong dropped when control-plane auth enabled', async (t) => {
  const clusterId = await Encryption.createClusterId('CLUSTER-PONG-A')
  const peerId = await Encryption.createId('peer-PONG-A')
  const signingKeys = await Encryption.createKeyPair('sig-PONG-A')

  const peer = new Peer(
    {
      config: {
        clusterId,
        peerId,
        address: '127.0.0.1',
        signingKeys,
        controlPlaneAuth: 'sig',
        keepalive: 1000
      }
    },
    createDgramStub()
  )

  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: peerId.toString(),
      responderPeerId: (await Encryption.createId('resp')).toString()
    }
  }

  const data = await Packet.encode(new PacketPong(props))
  await peer._onMessage(data, { port: 33333, address: '127.0.0.1' })
  t.equal(peer.metrics.i[PacketPong.type], 0, 'unsigned pong dropped')
})

test('signed pong accepted when control-plane auth enabled', async (t) => {
  const clusterId = await Encryption.createClusterId('CLUSTER-PONG-B')
  const peerId = await Encryption.createId('peer-PONG-B')
  const signingKeys = await Encryption.createKeyPair('sig-PONG-B')

  const peer = new Peer(
    {
      config: {
        clusterId,
        peerId,
        address: '127.0.0.1',
        signingKeys,
        controlPlaneAuth: 'sig',
        keepalive: 1000
      }
    },
    createDgramStub()
  )

  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: peerId.toString(),
      responderPeerId: (await Encryption.createId('resp2')).toString()
    }
  }
  peer._applyControlAuth(PacketPong, props)
  const data = await Packet.encode(new PacketPong(props))
  await peer._onMessage(data, { port: 33334, address: '127.0.0.1' })
  t.equal(peer.metrics.i[PacketPong.type], 1, 'signed pong accepted')
})
