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

test('concurrent streams compose independently', async (t) => {
  const clusterId = await Encryption.createClusterId('STRM-CONC')
  const peerId = await Encryption.createId('peer-STRM-CONC')
  const signingKeys = await Encryption.createKeyPair('sig-STRM-CONC')
  const sharedKeyA = await Encryption.createSharedKey('sub-A')
  const sharedKeyB = await Encryption.createSharedKey('sub-B')

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

  const keysA = await Encryption.createKeyPair(sharedKeyA)
  const keysB = await Encryption.createKeyPair(sharedKeyB)
  peer.encryption.add(keysA.publicKey, keysA.privateKey)
  peer.encryption.add(keysB.publicKey, keysB.privateKey)

  const fromA = (await Encryption.createId('from-A')).toString()
  const fromB = (await Encryption.createId('from-B')).toString()
  peer.peers.push({
    peerId: fromA,
    address: '127.0.0.1',
    port: 55556,
    natType: NAT.UNRESTRICTED
  })
  peer.peers.push({
    peerId: fromB,
    address: '127.0.0.1',
    port: 55557,
    natType: NAT.UNRESTRICTED
  })

  const payloadA = Buffer.alloc(2048, 0x41)
  const payloadB = Buffer.alloc(3072, 0x42)
  const sealedA = peer.encryption.seal(payloadA, keysA)
  const sealedB = peer.encryption.seal(payloadB, keysB)

  const mkArgs = (keys, from) => ({
    clusterId,
    subclusterId: keys.publicKey,
    usr3: Buffer.from(peerId, 'hex'),
    usr4: Buffer.from(from, 'hex')
  })
  const packetsA = await peer._message2packets(
    PacketStream,
    sealedA,
    mkArgs(keysA, fromA)
  )
  const packetsB = await peer._message2packets(
    PacketStream,
    sealedB,
    mkArgs(keysB, fromB)
  )

  let lenA = 0
  let lenB = 0
  peer.onStream = async (p, from) => {
    const opened = await peer.open(
      p.message,
      Buffer.from(p.subclusterId).toString('base64')
    )
    const l = Buffer.from(opened).length
    if (from.peerId === fromA) lenA = l
    if (from.peerId === fromB) lenB = l
  }

  const shuffled = [
    packetsA[1],
    packetsB[packetsB.length - 1],
    packetsA[0],
    packetsB[0],
    packetsA[packetsA.length - 1]
  ]
  for (const p of shuffled) {
    const buf = await Packet.encode(p)
    await peer._onMessage(buf, { port: 63000, address: '127.0.0.1' })
  }

  t.equal(lenA, payloadA.length, 'stream A composed')
  t.equal(lenB, payloadB.length, 'stream B composed')
})
