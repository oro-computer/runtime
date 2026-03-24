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

test('mapping endpoint dependent when reflection ports differ', async (t) => {
  const clusterId = await Encryption.createClusterId('NAT-DEP')
  const peerId = await Encryption.createId('peer-DEP')
  const signingKeys = await Encryption.createKeyPair('sig-DEP')
  const peer = new Peer(
    {
      config: {
        clusterId,
        peerId,
        address: '10.0.0.2',
        signingKeys,
        natType: NAT.UNRESTRICTED,
        keepalive: 1000
      }
    },
    createDgramStub()
  )

  // Speed up timeouts
  peer._setTimeout = (fn) => {
    fn()
    return 0
  }

  peer.reflectionId = 'deadbeef000001'
  peer.reflectionStage = 1
  const p1 = new PacketPong({
    message: {
      requesterPeerId: (await Encryption.createId('rqa')).toString(),
      responderPeerId: (await Encryption.createId('rsa')).toString(),
      isReflection: true,
      reflectionId: peer.reflectionId,
      port: 20001,
      address: '10.0.0.2'
    }
  })
  await peer._onProbeMessage(await Packet.encode(p1), {
    port: 20001,
    address: '198.51.100.10'
  })

  peer.reflectionId = 'deadbeef000002'
  peer.reflectionStage = 2
  const p2 = new PacketPong({
    message: {
      requesterPeerId: (await Encryption.createId('rqb')).toString(),
      responderPeerId: (await Encryption.createId('rsb')).toString(),
      isReflection: true,
      reflectionId: peer.reflectionId,
      port: 20002,
      address: '10.0.0.2'
    }
  })
  await peer._onProbeMessage(await Packet.encode(p2), {
    port: 20002,
    address: '198.51.100.11'
  })

  // Two reflection pongs with differing port via _onPong
  const respId = (await Encryption.createId('respX')).toString()
  const a = new PacketPong({
    message: {
      requesterPeerId: peerId.toString(),
      responderPeerId: respId,
      isReflection: true,
      reflectionId: peer.reflectionId,
      port: 51001,
      address: '10.0.0.2'
    }
  })
  const b = new PacketPong({
    message: {
      requesterPeerId: peerId.toString(),
      responderPeerId: respId,
      isReflection: true,
      reflectionId: peer.reflectionId,
      port: 51002,
      address: '10.0.0.2'
    }
  })
  await peer._onPong(a, 51001, '10.0.0.2')
  await peer._onPong(b, 51002, '10.0.0.2')

  t.ok(
    (peer.natType & NAT.MAPPING_ENDPOINT_DEPENDENT) ===
      NAT.MAPPING_ENDPOINT_DEPENDENT,
    'endpoint dependent mapping set'
  )
})
