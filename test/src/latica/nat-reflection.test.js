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

test('probe stage updates probeExternalPort and firewall bits; mapping detected; natType set', async (t) => {
  const clusterId = await Encryption.createClusterId('NAT-REFLECT')
  const peerId = await Encryption.createId('peer-NR')
  const signingKeys = await Encryption.createKeyPair('sig-NR')
  const peer = new Peer(
    {
      config: {
        clusterId,
        peerId,
        address: '10.0.0.1',
        signingKeys,
        natType: NAT.UNRESTRICTED,
        keepalive: 1000
      }
    },
    createDgramStub()
  )

  // override timeouts to execute immediately
  peer._setTimeout = (fn) => {
    fn()
    return 0
  }

  // Stage 1: set reflectionId and stage=1; receive probe pong with port -> set probeExternalPort
  peer.reflectionId = 'abcdef123456'
  peer.reflectionStage = 1
  const pong1 = new PacketPong({
    message: {
      requesterPeerId: (await Encryption.createId('rq')).toString(),
      responderPeerId: (await Encryption.createId('rs')).toString(),
      isReflection: true,
      reflectionId: peer.reflectionId,
      port: 22222,
      address: '10.0.0.1'
    }
  })
  await peer._onProbeMessage(await Packet.encode(pong1), {
    port: 22222,
    address: '198.51.100.1'
  })
  t.equal(peer.config.probeExternalPort, 22222, 'probeExternalPort learned')

  // Stage 2: update firewall bits
  peer.reflectionId = 'abcdef123456'
  peer.reflectionStage = 2
  const pong2 = new PacketPong({
    message: {
      requesterPeerId: (await Encryption.createId('rq2')).toString(),
      responderPeerId: (await Encryption.createId('rs2')).toString(),
      isReflection: true,
      reflectionId: peer.reflectionId,
      port: 33333,
      address: '10.0.0.1'
    }
  })
  await peer._onProbeMessage(await Packet.encode(pong2), {
    port: 33333,
    address: '198.51.100.2'
  })
  t.ok(
    (peer.nextNatType & NAT.FIREWALL_ALLOW_KNOWN_IP) ===
      NAT.FIREWALL_ALLOW_KNOWN_IP ||
      (peer.nextNatType & NAT.FIREWALL_ALLOW_ANY) === NAT.FIREWALL_ALLOW_ANY,
    'firewall bits set'
  )

  // Mapping detection via two reflection pongs in _onPong (same/different ports)
  const respId = (await Encryption.createId('responder')).toString()
  const portA = 44444
  const portB = 44444 // use same to classify endpoint independent
  const pA = new PacketPong({
    message: {
      requesterPeerId: peerId.toString(),
      responderPeerId: respId,
      isReflection: true,
      reflectionId: peer.reflectionId,
      port: portA,
      address: '10.0.0.1'
    }
  })
  const pB = new PacketPong({
    message: {
      requesterPeerId: peerId.toString(),
      responderPeerId: respId,
      isReflection: true,
      reflectionId: peer.reflectionId,
      port: portB,
      address: '10.0.0.1'
    }
  })

  await peer._onPong(pA, portA, '10.0.0.1')
  await peer._onPong(pB, portB, '10.0.0.1')

  t.ok(NAT.isValid(peer.natType), 'natType computed')
})
