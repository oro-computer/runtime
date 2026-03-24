import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import { Packet, PacketPong } from 'oro:latica/packets'

function createDgramStub () {
  class StubSocket {
    on () {}
    removeAllListeners () {}
    setMaxListeners () {}
    bind () {}
    address () {
      return { port: 0 }
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

test('probe pong requires signature under control-plane auth', async (t) => {
  const clusterId = await Encryption.createClusterId('NR-PROBE-AUTH')
  const peerId = await Encryption.createId('peer-NR-PROBE-AUTH')
  const signingKeys = await Encryption.createKeyPair('sig-NR-PROBE-AUTH')
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
  const pong = new PacketPong({
    ...base,
    message: {
      requesterPeerId: peerId.toString(),
      responderPeerId: (await Encryption.createId('r')).toString(),
      isReflection: true,
      reflectionId: 'abcdef123456'
    }
  })
  const buf = await Packet.encode(pong)
  await peer._onProbeMessage(buf, { port: 61200, address: '127.0.0.2' })
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
  await peer._onProbeMessage(await Packet.encode(new PacketPong(props)), {
    port: 61201,
    address: '127.0.0.3'
  })
  t.equal(
    peer.metrics.i[PacketPong.type],
    before + 1,
    'signed probe pong accepted'
  )
})
