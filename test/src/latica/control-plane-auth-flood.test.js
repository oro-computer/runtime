import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import {
  Packet,
  PacketPing,
  PacketPong,
  PacketIntro,
  PacketJoin,
  PacketQuery
} from 'oro:latica/packets'

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

test('auth flood: only signed control frames increment metrics', async (t) => {
  const clusterId = await Encryption.createClusterId('AUTH-FLOOD')
  const peerId = await Encryption.createId('peer-AUTH-FLOOD')
  const signingKeys = await Encryption.createKeyPair('sig-AUTH-FLOOD')
  const peer = new Peer(
    {
      config: {
        clusterId,
        peerId,
        address: '127.0.0.1',
        signingKeys,
        controlPlaneAuth: 'sig',
        natType: NAT.UNRESTRICTED,
        keepalive: 1000
      }
    },
    createDgramStub()
  )

  const subclusterId = new Uint8Array(32)
  const addr = '127.0.0.42'
  const port = 52042
  const mk = (Ctor, message) => ({ clusterId, subclusterId, message })

  const unsigned = [
    new PacketPing(
      mk(PacketPing, {
        requesterPeerId: peerId.toString(),
        natType: NAT.UNRESTRICTED
      })
    ),
    new PacketPong(
      mk(PacketPong, {
        requesterPeerId: peerId.toString(),
        responderPeerId: (await Encryption.createId('resp')).toString()
      })
    ),
    new PacketIntro(
      mk(PacketIntro, {
        requesterPeerId: (await Encryption.createId('rq')).toString(),
        responderPeerId: peerId.toString(),
        natType: NAT.UNRESTRICTED,
        address: '127.0.0.1',
        port: 1
      })
    ),
    new PacketJoin(
      mk(PacketJoin, {
        requesterPeerId: (await Encryption.createId('rq2')).toString(),
        natType: NAT.UNRESTRICTED,
        address: '127.0.0.1',
        port: 2
      })
    ),
    new PacketQuery(mk(PacketQuery, {}))
  ]

  for (const pkt of unsigned) {
    const buf = await Packet.encode(pkt)
    await peer._onMessage(buf, { port, address: addr })
  }

  const before = {
    ping: peer.metrics.i[PacketPing.type],
    pong: peer.metrics.i[PacketPong.type],
    intro: peer.metrics.i[PacketIntro.type],
    join: peer.metrics.i[PacketJoin.type],
    query: peer.metrics.i[PacketQuery.type]
  }

  // now signed
  const signed = [
    mk(PacketPing, {
      requesterPeerId: peerId.toString(),
      natType: NAT.UNRESTRICTED
    }),
    mk(PacketPong, {
      requesterPeerId: peerId.toString(),
      responderPeerId: (await Encryption.createId('resp3')).toString()
    }),
    mk(PacketIntro, {
      requesterPeerId: (await Encryption.createId('rq3')).toString(),
      responderPeerId: peerId.toString(),
      natType: NAT.UNRESTRICTED,
      address: '127.0.0.1',
      port: 3
    }),
    mk(PacketJoin, {
      requesterPeerId: (await Encryption.createId('rq4')).toString(),
      natType: NAT.UNRESTRICTED,
      address: '127.0.0.1',
      port: 4
    }),
    mk(PacketQuery, {})
  ]
  peer._applyControlAuth(PacketPing, signed[0])
  peer._applyControlAuth(PacketPong, signed[1])
  peer._applyControlAuth(PacketIntro, signed[2])
  peer._applyControlAuth(PacketJoin, signed[3])
  peer._applyControlAuth(PacketQuery, signed[4])

  const encoded = [
    await Packet.encode(new PacketPing(signed[0])),
    await Packet.encode(new PacketPong(signed[1])),
    await Packet.encode(new PacketIntro(signed[2])),
    await Packet.encode(new PacketJoin(signed[3])),
    await Packet.encode(new PacketQuery(signed[4]))
  ]
  for (const buf of encoded) await peer._onMessage(buf, { port, address: addr })

  t.equal(
    peer.metrics.i[PacketPing.type],
    before.ping + 1,
    'signed ping increments'
  )
  t.equal(
    peer.metrics.i[PacketPong.type],
    before.pong + 1,
    'signed pong increments'
  )
  t.equal(
    peer.metrics.i[PacketIntro.type],
    before.intro + 1,
    'signed intro increments'
  )
  t.equal(
    peer.metrics.i[PacketJoin.type],
    before.join + 1,
    'signed join increments'
  )
  t.equal(
    peer.metrics.i[PacketQuery.type],
    before.query + 1,
    'signed query increments'
  )
})
