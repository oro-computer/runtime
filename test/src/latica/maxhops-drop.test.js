import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import { Packet, PacketPublish } from 'oro:latica/packets'

function createDgramStub () {
  class S {
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
      return new S()
    }
  }
}

test('onPublish does not mcast when hops >= maxHops', async (t) => {
  const clusterId = await Encryption.createClusterId('HOPS')
  const peerId = await Encryption.createId('peer-HOPS')
  const signingKeys = await Encryption.createKeyPair('sig-HOPS')
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

  let sent = 0
  peer.send = async () => {
    sent++
  }

  const pkt = new PacketPublish({
    clusterId,
    subclusterId: new Uint8Array(32),
    message: Buffer.from('hops')
  })
  const buf = await Packet.encode(pkt)
  const p = Packet.decode(buf)
  p.hops = peer.maxHops

  await peer._onPublish(p, 1, '127.0.0.1')
  t.equal(sent, 0, 'no mcast when at maxHops')
})
