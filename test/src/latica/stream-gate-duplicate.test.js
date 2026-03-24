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

test('duplicate stream fragment gated and not processed twice', async (t) => {
  const clusterId = await Encryption.createClusterId('STRM-GATE')
  const peerId = await Encryption.createId('peer-STRM-GATE')
  const signingKeys = await Encryption.createKeyPair('sig-STRM-GATE')
  const sharedKey = await Encryption.createSharedKey('sub-GATE')

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
  const keys = await Encryption.createKeyPair(sharedKey)

  const to = (await Encryption.createId('to')).toString()
  const from = (await Encryption.createId('from')).toString()
  const sealed = peer.encryption.seal(Buffer.from('dup'), keys)
  const args = {
    clusterId,
    subclusterId: keys.publicKey,
    usr3: Buffer.from(to, 'hex'),
    usr4: Buffer.from(from, 'hex')
  }
  const packets = await peer._message2packets(PacketStream, sealed, args)
  const frag = packets[0]
  const buf = await Packet.encode(frag)

  const before = peer.metrics.i[PacketStream.type]
  await peer._onMessage(buf, { port: 41000, address: '127.0.0.1' })
  await peer._onMessage(buf, { port: 41000, address: '127.0.0.1' })
  const after = peer.metrics.i[PacketStream.type]

  t.equal(
    after,
    before + 1,
    'second duplicate did not increment processing count'
  )
})
