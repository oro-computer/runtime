import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
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

test('reflection pong requires signature under control-plane auth', async (t) => {
  const clusterId = await Encryption.createClusterId('NR-AUTH')
  const peerId = await Encryption.createId('peer-NR-AUTH')
  const signingKeys = await Encryption.createKeyPair('sig-NR-AUTH')
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

  const base = { clusterId, subclusterId: new Uint8Array(32) }
  const unsigned = new PacketPong({
    ...base,
    message: {
      requesterPeerId: peerId.toString(),
      responderPeerId: (await Encryption.createId('r')).toString(),
      isReflection: true,
      reflectionId: 'abcdef123456'
    }
  })
  await peer._onMessage(await Packet.encode(unsigned), {
    port: 60100,
    address: '127.0.0.1'
  })
  const before = peer.metrics.i[PacketPong.type]

  const props = {
    ...base,
    message: {
      requesterPeerId: peerId.toString(),
      responderPeerId: (await Encryption.createId('r2')).toString(),
      isReflection: true,
      reflectionId: 'abcdef123456'
    }
  }
  peer._applyControlAuth(PacketPong, props)
  await peer._onMessage(await Packet.encode(new PacketPong(props)), {
    port: 60101,
    address: '127.0.0.1'
  })
  t.equal(
    peer.metrics.i[PacketPong.type],
    before + 1,
    'signed reflection pong accepted'
  )
})
