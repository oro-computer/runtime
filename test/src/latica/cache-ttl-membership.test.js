import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import { Packet, PacketPublish } from 'oro:latica/packets'

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

test('membership doubles default TTL for cache expiration', async (t) => {
  const clusterA = await Encryption.createClusterId('TTL-A')
  const clusterB = await Encryption.createClusterId('TTL-B')
  const peerId = await Encryption.createId('peer-TTL')
  const signingKeys = await Encryption.createKeyPair('sig-TTL')
  const peer = new Peer(
    {
      config: {
        clusterId: clusterA,
        peerId,
        address: '127.0.0.1',
        signingKeys,
        natType: NAT.UNRESTRICTED,
        keepalive: 1000
      }
    },
    createDgramStub()
  )

  const now = Date.now()
  const expiredBy = 1000

  // packet in cluster A (member) – should not expire at TTL+epsilon
  const pA = new PacketPublish({
    clusterId: clusterA,
    subclusterId: new Uint8Array(32),
    message: Buffer.from('A')
  })
  const bA = await Packet.encode(pA)
  const dA = Packet.decode(bA)
  dA.timestamp = now - (Packet.ttl + expiredBy)
  const kA = dA.packetId.toString('hex')

  // packet in cluster B (non-member) – should expire at TTL+epsilon
  const pB = new PacketPublish({
    clusterId: clusterB,
    subclusterId: new Uint8Array(32),
    message: Buffer.from('B')
  })
  const bB = await Packet.encode(pB)
  const dB = Packet.decode(bB)
  dB.timestamp = now - (Packet.ttl + expiredBy)
  const kB = dB.packetId.toString('hex')

  peer.cacheInsert(dA)
  peer.cacheInsert(dB)

  await peer._mainLoop(now)

  t.equal(
    peer.cache.has(kA),
    true,
    'member cluster retains packet (TTL doubled)'
  )
  t.equal(peer.cache.has(kB), false, 'non-member cluster packet expired')
})
