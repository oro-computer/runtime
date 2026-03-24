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

test('cache entries with small ttl expire on mainLoop', async (t) => {
  const clusterId = await Encryption.createClusterId('CACHE-TTL')
  const peerId = await Encryption.createId('peer-CACHE-TTL')
  const signingKeys = await Encryption.createKeyPair('sig-CACHE-TTL')
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

  // create a publish with ttl=500ms and past timestamp
  const pkt = new PacketPublish({
    clusterId,
    subclusterId: new Uint8Array(32),
    message: Buffer.from('ttl'),
    ttl: 500
  })
  const buf = await Packet.encode(pkt)
  const p = Packet.decode(buf)
  p.timestamp = Date.now() - 1000

  const k = p.packetId.toString('hex')
  peer.cacheInsert(p)
  t.equal(peer.cache.has(k), true, 'cache initially has entry')

  await peer._mainLoop(Date.now())
  t.equal(peer.cache.has(k), false, 'cache entry expired and removed')
})
