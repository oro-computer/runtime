import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import { PacketPublish, PacketStream } from 'oro:latica/packets'

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

test('publish split boundary: 1024 vs 1025', async (t) => {
  const clusterId = await Encryption.createClusterId('BOUND')
  const peerId = await Encryption.createId('peer-BOUND')
  const signingKeys = await Encryption.createKeyPair('sig-BOUND')
  const sharedKey = await Encryption.createSharedKey('shared-BOUND')

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

  const payload1024 = Buffer.alloc(1024, 0x7a)
  const sealed1024 = peer.encryption.seal(payload1024, keys)
  const packets1024 = await peer._message2packets(PacketPublish, sealed1024, {
    clusterId,
    subclusterId: keys.publicKey
  })
  t.equal(packets1024.length, 1, 'exactly 1024 → single packet')
  t.equal(packets1024[0].index, -1, 'single packet has index -1')

  const payload1025 = Buffer.alloc(1025, 0x7b)
  const sealed1025 = peer.encryption.seal(payload1025, keys)
  const packets1025 = await peer._message2packets(PacketPublish, sealed1025, {
    clusterId,
    subclusterId: keys.publicKey
  })
  t.ok(packets1025.length > 1, '1025 bytes → split across multiple packets')
  t.equal(packets1025[0].index, 0, 'head index = 0 when split')
})

test('stream split boundary: 1024 vs 1025', async (t) => {
  const clusterId = await Encryption.createClusterId('BOUND-S')
  const peerId = await Encryption.createId('peer-BOUND-S')
  const signingKeys = await Encryption.createKeyPair('sig-BOUND-S')
  const sharedKey = await Encryption.createSharedKey('shared-BOUND-S')

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

  const mkArgs = async () => ({
    clusterId,
    subclusterId: keys.publicKey,
    usr3: Buffer.from(peerId, 'hex'),
    usr4: Buffer.from((await Encryption.createId('f')).toString(), 'hex')
  })

  const payload1024 = Buffer.alloc(1024, 0x70)
  const sealed1024 = peer.encryption.seal(payload1024, keys)
  const packets1024 = await peer._message2packets(
    PacketStream,
    sealed1024,
    await mkArgs()
  )
  t.equal(packets1024.length, 1, 'exactly 1024 → single packet (stream)')
  t.equal(packets1024[0].index, -1, 'single stream packet has index -1')

  const payload1025 = Buffer.alloc(1025, 0x71)
  const sealed1025 = peer.encryption.seal(payload1025, keys)
  const packets1025 = await peer._message2packets(
    PacketStream,
    sealed1025,
    await mkArgs()
  )
  t.ok(packets1025.length > 1, '1025 bytes → split stream')
  t.equal(packets1025[0].index, 0, 'head index = 0 when split (stream)')
})
