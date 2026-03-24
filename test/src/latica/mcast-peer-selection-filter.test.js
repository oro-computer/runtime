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

test('mcast excludes stale and NAT-invalid peers, includes valid set', async (t) => {
  const clusterId = await Encryption.createClusterId('MCAST-FILTER')
  const peerId = await Encryption.createId('peer-MCAST-FILTER')
  const signingKeys = await Encryption.createKeyPair('sig-MCAST-FILTER')
  const sharedKey = await Encryption.createSharedKey('mcast-filter')
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

  // stale peer (lastUpdate too old)
  const stale = {
    peerId: (await Encryption.createId('stale')).toString(),
    address: '127.0.0.20',
    port: 30100,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now() - keepalive * 5
  }
  // NAT invalid
  const invalidNat = {
    peerId: (await Encryption.createId('invnat')).toString(),
    address: '127.0.0.21',
    port: 30101,
    natType: 0,
    lastUpdate: Date.now()
  }
  // valid peers
  const valid1 = {
    peerId: (await Encryption.createId('v1')).toString(),
    address: '127.0.0.22',
    port: 30102,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now()
  }
  const valid2 = {
    peerId: (await Encryption.createId('v2')).toString(),
    address: '127.0.0.23',
    port: 30103,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now()
  }

  peer.peers.push(stale, invalidNat, valid1, valid2)

  const sent = []
  peer.send = async (buf, port, address) => {
    sent.push([address, port])
  }

  const pkt = new PacketPublish({
    clusterId,
    subclusterId: keys.publicKey,
    message: Buffer.from('m')
  })
  const buf = await Packet.encode(pkt)
  const p = Packet.decode(buf)
  await peer.mcast(p)

  const addrs = new Set(sent.map(([a, _]) => a))
  t.equal(addrs.has(stale.address), false, 'stale excluded')
  t.equal(addrs.has(invalidNat.address), false, 'NAT invalid excluded')
  t.equal(
    addrs.has(valid1.address) || addrs.has(valid2.address),
    true,
    'valid peers included'
  )
})
