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

function shuffle (arr) {
  for (let i = arr.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1))
    ;[arr[i], arr[j]] = [arr[j], arr[i]]
  }
  return arr
}

test('five shuffled streams with larger payloads compose correctly', async (t) => {
  const clusterId = await Encryption.createClusterId('STRM-MULTI-L')
  const peerId = await Encryption.createId('peer-STRM-MULTI-L')
  const signingKeys = await Encryption.createKeyPair('sig-STRM-MULTI-L')
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

  const streams = []
  for (let s = 0; s < 5; s++) {
    const sharedKey = await Encryption.createSharedKey('ls' + s)
    const keys = await Encryption.createKeyPair(sharedKey)
    peer.encryption.add(keys.publicKey, keys.privateKey)
    const from = (await Encryption.createId('lfrom' + s)).toString()
    peer.peers.push({
      peerId: from,
      address: '127.0.0.1',
      port: 60000 + s,
      natType: NAT.UNRESTRICTED
    })
    const len = 4096 + s * 1024
    const payload = Buffer.alloc(len, 0x50 + s)
    const sealed = peer.encryption.seal(payload, keys)
    const args = {
      clusterId,
      subclusterId: keys.publicKey,
      usr3: Buffer.from(peerId, 'hex'),
      usr4: Buffer.from(from, 'hex')
    }
    const packets = await peer._message2packets(PacketStream, sealed, args)
    streams.push({ payload, keys, packets })
  }

  const arrivals = shuffle(streams.flatMap((st) => st.packets))
  const received = new Map()
  peer.onStream = async (p) => {
    const opened = await peer.open(
      p.message,
      Buffer.from(p.subclusterId).toString('base64')
    )
    received.set(
      Buffer.from(p.subclusterId).toString('hex'),
      Buffer.from(opened).length
    )
  }

  for (const p of arrivals) {
    const buf = await Packet.encode(p)
    await peer._onMessage(buf, { port: 65000, address: '127.0.0.1' })
  }

  for (const st of streams) {
    t.equal(
      received.get(Buffer.from(st.keys.publicKey).toString('hex')),
      st.payload.length,
      'stream composed correctly'
    )
  }
})
