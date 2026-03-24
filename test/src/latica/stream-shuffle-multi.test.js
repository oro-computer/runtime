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
    const j = Math.floor((((i * 9301 + 49297) % 233280) / 233280) * (i + 1)) // poor-man det rand
    const tmp = arr[i]
    arr[i] = arr[j]
    arr[j] = tmp
  }
  return arr
}

test('multiple shuffled streams compose correctly', async (t) => {
  const clusterId = await Encryption.createClusterId('STRM-MULTI')
  const peerId = await Encryption.createId('peer-STRM-MULTI')
  const signingKeys = await Encryption.createKeyPair('sig-STRM-MULTI')
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
  for (let s = 0; s < 3; s++) {
    const sharedKey = await Encryption.createSharedKey('s' + s)
    const keys = await Encryption.createKeyPair(sharedKey)
    peer.encryption.add(keys.publicKey, keys.privateKey)
    const from = (await Encryption.createId('from' + s)).toString()
    peer.peers.push({
      peerId: from,
      address: '127.0.0.1',
      port: 51000 + s,
      natType: NAT.UNRESTRICTED
    })
    const len = 2048 + s * 512
    const payload = Buffer.alloc(len, 0x30 + s)
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
    await peer._onMessage(buf, { port: 52000, address: '127.0.0.1' })
  }

  for (const st of streams) {
    t.equal(
      received.get(Buffer.from(st.keys.publicKey).toString('hex')),
      st.payload.length,
      'stream composed of expected length'
    )
  }
})
