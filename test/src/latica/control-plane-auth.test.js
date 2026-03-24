import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import { Packet, PacketPing } from 'oro:latica/packets'
// minimal dgram stub to avoid real network
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

test('control-plane unsigned ping is dropped when auth enabled', async (t) => {
  const clusterId = await Encryption.createClusterId('TEST-CP')
  const peerId = await Encryption.createId('peer-A')
  const signingKeys = await Encryption.createKeyPair('sig-A')

  const dgram = createDgramStub()
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
    dgram
  )

  // inbound unsigned ping (missing usr2/sig)
  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: (await Encryption.createId('peer-B')).toString(),
      natType: NAT.UNRESTRICTED
    }
  }
  const data = await Packet.encode(new PacketPing(props))
  await peer._onMessage(data, { port: 5555, address: '127.0.0.1' })
  t.equal(peer.metrics.i[PacketPing.type], 0, 'unsigned ping dropped')
})

test('control-plane signed ping is accepted when auth enabled', async (t) => {
  const clusterId = await Encryption.createClusterId('TEST-CP2')
  const peerId = await Encryption.createId('peer-C')
  const signingKeys = await Encryption.createKeyPair('sig-C')

  const dgram = createDgramStub()
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
    dgram
  )

  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: (await Encryption.createId('peer-D')).toString(),
      natType: NAT.UNRESTRICTED
    }
  }

  // use internal helper to sign the props
  peer._applyControlAuth(PacketPing, props)
  const data = await Packet.encode(new PacketPing(props))
  await peer._onMessage(data, { port: 5556, address: '127.0.0.1' })
  t.equal(peer.metrics.i[PacketPing.type], 1, 'signed ping accepted')
})
