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

test('mcast sends to peers once and gates subsequent calls', async (t) => {
  const clusterId = await Encryption.createClusterId('MCAST')
  const peerId = await Encryption.createId('peer-MCAST')
  const signingKeys = await Encryption.createKeyPair('sig-MCAST')
  const sharedKey = await Encryption.createSharedKey('mcast')
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
  // add some peers
  peer.peers.push({
    peerId: (await Encryption.createId('p1')).toString(),
    address: '127.0.0.2',
    port: 21001,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now()
  })
  peer.peers.push({
    peerId: (await Encryption.createId('p2')).toString(),
    address: '127.0.0.3',
    port: 21002,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now()
  })
  peer.peers.push({
    peerId: (await Encryption.createId('p3')).toString(),
    address: '127.0.0.4',
    port: 21003,
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
    message: Buffer.from('z')
  })
  // compute packetId by encoding
  const buf = await Packet.encode(pkt)
  const p = Packet.decode(buf)
  await peer.mcast(p)
  const once = sent
  await peer.mcast(p)
  t.equal(sent, once, 'second mcast gated, no additional sends')
})
