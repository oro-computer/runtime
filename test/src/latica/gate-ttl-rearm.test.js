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

test('gate entry expires after mainLoop and allows re-mcast', async (t) => {
  const clusterId = await Encryption.createClusterId('GATE-TTL')
  const peerId = await Encryption.createId('peer-GATE-TTL')
  const signingKeys = await Encryption.createKeyPair('sig-GATE-TTL')
  const sharedKey = await Encryption.createSharedKey('gttl')
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

  // Add two valid peers to ensure mcast sends
  peer.peers.push({
    peerId: (await Encryption.createId('p1')).toString(),
    address: '127.0.0.2',
    port: 21010,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now()
  })
  peer.peers.push({
    peerId: (await Encryption.createId('p2')).toString(),
    address: '127.0.0.3',
    port: 21011,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now()
  })

  let sent = 0
  peer.send = async () => {
    sent++
  }

  const pkt = new PacketPublish({
    clusterId,
    subclusterId: keys.publicKey,
    message: Buffer.from('g')
  })
  const buf = await Packet.encode(pkt)
  const p = Packet.decode(buf)

  await peer.mcast(p)
  const first = sent
  await peer.mcast(p)
  t.equal(sent, first, 'second mcast gated')

  // expire gates
  await peer._mainLoop(Date.now())

  await peer.mcast(p)
  t.ok(sent > first, 're-mcast after gate expiry sends again')
})
