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

test('subcluster-specific rateLimit enforces low threshold', async (t) => {
  const clusterId = await Encryption.createClusterId('SUB-QUOTA')
  const peerId = await Encryption.createId('peer-SUB-QUOTA')
  const signingKeys = await Encryption.createKeyPair('sig-SUB-QUOTA')
  const sharedKey = await Encryption.createSharedKey('sub-q')
  const keys = await Encryption.createKeyPair(sharedKey)

  const keepalive = 1000
  const peer = new Peer(
    {
      config: {
        clusterId,
        peerId,
        address: '127.0.0.1',
        signingKeys,
        natType: NAT.UNRESTRICTED,
        keepalive
      }
    },
    createDgramStub()
  )

  // configure cluster/subcluster with strict rateLimit
  const cid = Buffer.from(clusterId).toString('base64')
  const scid = Buffer.from(keys.publicKey).toString('base64')
  peer.clusters[cid] = { [scid]: { rateLimit: 10 } }

  const addr = '198.51.100.200'
  const port = 58000
  const before = peer.metrics.i.DROPPED

  for (let i = 0; i < 50; i++) {
    const pkt = new PacketPublish({
      clusterId,
      subclusterId: keys.publicKey,
      message: Buffer.from('q' + i)
    })
    const buf = await Packet.encode(pkt)
    await peer._onMessage(buf, { port, address: addr })
  }

  t.ok(
    peer.metrics.i.DROPPED > before,
    'drops increased under strict subcluster quota'
  )
})
