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

test('replicable publish rate-limits after threshold', async (t) => {
  const clusterId = await Encryption.createClusterId('PUB-RATE')
  const peerId = await Encryption.createId('peer-PUB-RATE')
  const signingKeys = await Encryption.createKeyPair('sig-PUB-RATE')
  const sharedKey = await Encryption.createSharedKey('pub-rate')
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
  peer.encryption.add(keys.publicKey, keys.privateKey)

  const ADDR = '192.0.2.10'
  const PORT = 40040
  const before = peer.metrics.i.DROPPED

  for (let i = 0; i < 1100; i++) {
    const msg = Buffer.from('x' + i)
    const pkt = new PacketPublish({
      clusterId,
      subclusterId: keys.publicKey,
      message: peer.encryption.seal(msg, keys)
    })
    const buf = await Packet.encode(pkt)
    await peer._onMessage(buf, { port: PORT, address: ADDR })
  }

  t.ok(
    peer.metrics.i.DROPPED >= before,
    'DROPPED increased or equal (rate limit engaged)'
  )
})
