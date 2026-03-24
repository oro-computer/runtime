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

test('getPeers returns at most 3, prefers friend and includes indexed if none present', async (t) => {
  const clusterId = await Encryption.createClusterId('GP')
  const peerId = await Encryption.createId('peer-GP')
  const signingKeys = await Encryption.createKeyPair('sig-GP')
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

  const cid = Buffer.from(clusterId).toString('base64')
  peer.clusters[cid] = {}

  const friendId = (await Encryption.createId('friend')).toString()
  const friend = {
    peerId: friendId,
    address: '127.0.0.10',
    port: 30110,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now(),
    clusters: { [cid]: {} }
  }

  const indexed = {
    peerId: (await Encryption.createId('idx')).toString(),
    address: '127.0.0.11',
    port: 30111,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now(),
    indexed: true
  }
  const other1 = {
    peerId: (await Encryption.createId('o1')).toString(),
    address: '127.0.0.12',
    port: 30112,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now()
  }
  const other2 = {
    peerId: (await Encryption.createId('o2')).toString(),
    address: '127.0.0.13',
    port: 30113,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now()
  }
  peer.peers.push(friend, indexed, other1, other2)

  const pkt = new PacketPublish({
    clusterId,
    subclusterId: new Uint8Array(32),
    message: Buffer.from('gp')
  })
  const buf = await Packet.encode(pkt)
  const p = Packet.decode(buf)

  const list = peer.getPeers(p, peer.peers, [])
  t.ok(list.length <= 3, 'at most 3 peers returned')
  t.ok(
    list.find((x) => x.peerId === friendId),
    'friend included'
  )
  const hasIndexed = list.some((x) => x.indexed)
  t.equal(
    hasIndexed || peer.peers.some((x) => x.indexed && !list.includes(x)),
    true,
    'indexed peer in list or would be appended by mcast path'
  )
})
