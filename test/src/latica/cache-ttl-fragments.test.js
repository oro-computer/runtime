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

test('fragmented publish packets expire from cache on TTL', async (t) => {
  const clusterId = await Encryption.createClusterId('TTL-FRAG')
  const peerId = await Encryption.createId('peer-TTL-FRAG')
  const signingKeys = await Encryption.createKeyPair('sig-TTL-FRAG')
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

  const payload = Buffer.alloc(2048, 0x21)
  const pkt = new PacketPublish({
    clusterId,
    subclusterId: new Uint8Array(32),
    message: payload,
    ttl: 500
  })
  const buf = await Packet.encode(pkt)
  const head = Packet.decode(buf)
  // turn into fragments by setting index and previousId/nextId
  const frag1 = head.copy()
  frag1.index = 0
  frag1.timestamp = Date.now() - 1000
  const frag2 = head.copy()
  frag2.index = 1
  frag2.previousId = Buffer.from(frag1.packetId)
  frag2.timestamp = Date.now() - 1000

  const k1 = frag1.packetId.toString('hex')
  const k2 = frag2.packetId.toString('hex')
  peer.cacheInsert(frag1)
  peer.cacheInsert(frag2)
  t.equal(peer.cache.has(k1) && peer.cache.has(k2), true, 'fragments inserted')

  await peer._mainLoop(Date.now())
  t.equal(
    peer.cache.has(k1) || peer.cache.has(k2),
    false,
    'fragments expired and removed'
  )
})
