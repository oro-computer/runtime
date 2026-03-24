import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import { Packet, PacketPublish } from 'oro:latica/packets'

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

test('mcast prefers friends (same cluster) and ensures an indexed peer is included', async (t) => {
  const clusterId = await Encryption.createClusterId('MCAST-FRIEND')
  const peerId = await Encryption.createId('peer-MCAST-FRIEND')
  const signingKeys = await Encryption.createKeyPair('sig-MCAST-FRIEND')
  const sharedKey = await Encryption.createSharedKey('mcast-friend')
  const keys = await Encryption.createKeyPair(sharedKey)

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
  // mark membership in cluster
  peer.clusters[cid] = {}

  // add peers: two non-friends, one friend (same cluster), and one indexed
  const friendPeerId = (await Encryption.createId('friend')).toString()
  const friend = {
    peerId: friendPeerId,
    address: '127.0.0.10',
    port: 30010,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now(),
    clusters: { [cid]: {} }
  }
  peer.peers.push({
    peerId: (await Encryption.createId('n1')).toString(),
    address: '127.0.0.11',
    port: 30011,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now()
  })
  peer.peers.push({
    peerId: (await Encryption.createId('n2')).toString(),
    address: '127.0.0.12',
    port: 30012,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now()
  })
  const indexed = {
    peerId: (await Encryption.createId('idx')).toString(),
    address: '127.0.0.13',
    port: 30013,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now(),
    indexed: true
  }
  peer.peers.push(indexed)
  peer.peers.push(friend)

  const sentTo = []
  peer.send = async (buf, port, address) => {
    sentTo.push([address, port])
  }

  const pkt = new PacketPublish({
    clusterId,
    subclusterId: keys.publicKey,
    message: Buffer.from('friend')
  })
  const buf = await Packet.encode(pkt)
  const p = Packet.decode(buf)
  await peer.mcast(p)

  const friendIncluded = sentTo.some(
    ([addr, port]) => addr === friend.address && port === friend.port
  )
  const indexedIncluded = sentTo.some(
    ([addr, port]) => addr === indexed.address && port === indexed.port
  )

  t.equal(friendIncluded, true, 'friend peer included in fanout')
  t.equal(indexedIncluded, true, 'indexed peer included in fanout')
})
