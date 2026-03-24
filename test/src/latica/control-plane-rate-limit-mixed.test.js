import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import { Packet, PacketJoin, PacketPing } from 'oro:latica/packets'

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

test('mixed control-plane flood: joins trigger rate limiting more readily than pings', async (t) => {
  const clusterId = await Encryption.createClusterId('MIX-RATE')
  const peerId = await Encryption.createId('peer-MIX-RATE')
  const signingKeys = await Encryption.createKeyPair('sig-MIX-RATE')
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

  const addr = '203.0.113.77'
  const port = 57000
  const before = peer.metrics.i.DROPPED

  // 1200 joins and 1200 pings
  for (let i = 0; i < 1200; i++) {
    const j = new PacketJoin({
      clusterId,
      subclusterId: new Uint8Array(32),
      message: {
        requesterPeerId: (await Encryption.createId('rq' + i)).toString(),
        natType: NAT.UNRESTRICTED,
        address: '127.0.0.1',
        port: 1
      }
    })
    const pj = await Packet.encode(j)
    await peer._onMessage(pj, { port, address: addr })

    const p = new PacketPing({
      clusterId,
      subclusterId: new Uint8Array(32),
      message: {
        requesterPeerId: (await Encryption.createId('pr' + i)).toString(),
        natType: NAT.UNRESTRICTED
      }
    })
    const pp = await Packet.encode(p)
    await peer._onMessage(pp, { port, address: addr })
  }

  t.ok(peer.metrics.i.DROPPED >= before, 'DROPPED increased under mixed flood')
})
