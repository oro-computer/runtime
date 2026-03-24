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

test('out-of-order stream arrival composes once head arrives', async (t) => {
  const clusterId = await Encryption.createClusterId('STRM-OOO')
  const peerId = await Encryption.createId('peer-STRM-OOO')
  const signingKeys = await Encryption.createKeyPair('sig-STRM-OOO')
  const sharedKey = await Encryption.createSharedKey('sub-OOO')

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

  // register subcluster keys
  const keys = await Encryption.createKeyPair(sharedKey)
  peer.encryption.add(keys.publicKey, keys.privateKey)

  const streamFrom = (await Encryption.createId('from-OOO')).toString()
  peer.peers.push({
    peerId: streamFrom,
    address: '127.0.0.1',
    port: 55555,
    natType: NAT.UNRESTRICTED
  })

  const payload = Buffer.alloc(3072, 0x63)
  const sealed = peer.encryption.seal(payload, keys)
  const args = {
    clusterId,
    subclusterId: keys.publicKey,
    usr3: Buffer.from(peerId, 'hex'),
    usr4: Buffer.from(streamFrom, 'hex')
  }
  const packets = await peer._message2packets(PacketStream, sealed, args)

  let composed = 0
  peer.onStream = async (p) => {
    const opened = await peer.open(
      p.message,
      Buffer.from(p.subclusterId).toString('base64')
    )
    composed = Buffer.from(opened).length
  }

  // Send tail first (out of order), then head (index 0), then middle
  const tail = packets[packets.length - 1]
  const head = packets[0]
  const mid = packets[1]

  for (const p of [tail, head, mid]) {
    const buf = await Packet.encode(p)
    await peer._onMessage(buf, { port: 62000, address: '127.0.0.1' })
  }

  t.equal(
    composed,
    payload.length,
    'composed length matches after out-of-order arrival'
  )
})
