import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import { Packet, PacketStream } from 'oro:latica/packets'

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

test('stream traffic not rate-limited under generous defaults', async (t) => {
  const clusterId = await Encryption.createClusterId('STRM-RATE')
  const peerId = await Encryption.createId('peer-STRM-RATE')
  const signingKeys = await Encryption.createKeyPair('sig-STRM-RATE')
  const sharedKey = await Encryption.createSharedKey('sub-strm-rate')
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

  const ADDR = '203.0.113.50'
  const PORT = 48000
  const before = peer.metrics.i.DROPPED

  for (let i = 0; i < 5000; i++) {
    const pkt = new PacketStream({
      clusterId,
      subclusterId: keys.publicKey,
      usr3: Buffer.from(peerId, 'hex'),
      usr4: Buffer.from(peerId, 'hex'),
      message: Buffer.from('s' + i)
    })
    const buf = await Packet.encode(pkt)
    await peer._onMessage(buf, { port: PORT, address: ADDR })
  }

  t.equal(
    peer.metrics.i.DROPPED,
    before,
    'no drops for stream spam under default quota'
  )
})
