import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import { Packet, PacketIntro } from 'oro:latica/packets'

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

test('unsigned intro dropped when control-plane auth enabled', async (t) => {
  const clusterId = await Encryption.createClusterId('CLUSTER-INTRO-A')
  const peerId = await Encryption.createId('peer-INTRO-A')
  const signingKeys = await Encryption.createKeyPair('sig-INTRO-A')

  const peer = new Peer(
    {
      config: {
        clusterId,
        peerId,
        address: '127.0.0.1',
        signingKeys,
        controlPlaneAuth: 'sig',
        natType: NAT.UNRESTRICTED,
        keepalive: 1000
      }
    },
    createDgramStub()
  )

  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: (await Encryption.createId('rq')).toString(),
      responderPeerId: peerId.toString(),
      natType: NAT.UNRESTRICTED,
      address: '127.0.0.1',
      port: 11111
    }
  }

  const data = await Packet.encode(new PacketIntro(props))
  await peer._onMessage(data, { port: 11111, address: '127.0.0.1' })
  t.equal(peer.metrics.i[PacketIntro.type], 0, 'unsigned intro dropped')
})

test('signed intro accepted when control-plane auth enabled', async (t) => {
  const clusterId = await Encryption.createClusterId('CLUSTER-INTRO-B')
  const peerId = await Encryption.createId('peer-INTRO-B')
  const signingKeys = await Encryption.createKeyPair('sig-INTRO-B')

  const peer = new Peer(
    {
      config: {
        clusterId,
        peerId,
        address: '127.0.0.1',
        signingKeys,
        controlPlaneAuth: 'sig',
        natType: NAT.UNRESTRICTED,
        keepalive: 1000
      }
    },
    createDgramStub()
  )

  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: (await Encryption.createId('rq2')).toString(),
      responderPeerId: peerId.toString(),
      natType: NAT.UNRESTRICTED,
      address: '127.0.0.1',
      port: 22222
    }
  }
  peer._applyControlAuth(PacketIntro, props)
  const data = await Packet.encode(new PacketIntro(props))
  await peer._onMessage(data, { port: 22222, address: '127.0.0.1' })
  t.equal(peer.metrics.i[PacketIntro.type], 1, 'signed intro accepted')
})
