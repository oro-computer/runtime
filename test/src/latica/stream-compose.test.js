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

test('peer composes multi-chunk stream and invokes onStream with composed packet', async (t) => {
  const clusterId = await Encryption.createClusterId('CLUSTER-STRM')
  const peerId = await Encryption.createId('peer-STRM')
  const signingKeys = await Encryption.createKeyPair('sig-STRM')
  const sharedKey = await Encryption.createSharedKey('sub-STRM')

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

  // register subcluster keys so decryption is possible
  const keys = await Encryption.createKeyPair(sharedKey)
  peer.encryption.add(keys.publicKey, keys.privateKey)

  // stub sender peer so onStream has a source peer
  const streamFrom = (await Encryption.createId('from-STRM')).toString()
  peer.peers.push({
    peerId: streamFrom,
    address: '127.0.0.1',
    port: 54321,
    natType: NAT.UNRESTRICTED
  })

  const payload = Buffer.alloc(4096, 0x62) // 4 KiB "b"
  const sealed = peer.encryption.seal(payload, keys)

  const toHex = (s) => s
  const args = {
    clusterId,
    subclusterId: keys.publicKey,
    usr3: Buffer.from(toHex(peerId), 'hex'),
    usr4: Buffer.from(streamFrom, 'hex')
  }
  const packets = await peer._message2packets(PacketStream, sealed, args)

  let composedLen = 0
  peer.onStream = async (p /* composed Packet */, from) => {
    const opened = await peer.open(
      p.message,
      Buffer.from(p.subclusterId).toString('base64')
    )
    composedLen = Buffer.from(opened).length
    t.equal(from.peerId, streamFrom, 'onStream called with source peer')
  }

  // deliver chunks in order
  for (const p of packets) {
    const buf = await Packet.encode(p)
    await peer._onMessage(buf, { port: 60000, address: '127.0.0.1' })
  }

  t.equal(composedLen, payload.length, 'composed payload length matches')
})

test('peer relays stream to peerTo when not the destination', async (t) => {
  const clusterId = await Encryption.createClusterId('CLUSTER-STRM-R')
  const peerId = await Encryption.createId('peer-STRM-R')
  const signingKeys = await Encryption.createKeyPair('sig-STRM-R')
  const sharedKey = await Encryption.createSharedKey('sub-STRM-R')

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

  // keys for stream
  const keys = await Encryption.createKeyPair(sharedKey)

  // destination is another peer
  const streamTo = (await Encryption.createId('to-STRM-R')).toString()
  peer.peers.push({
    peerId: streamTo,
    address: '127.0.0.2',
    port: 23456,
    natType: NAT.UNRESTRICTED
  })

  const sealed = peer.encryption.seal(Buffer.from('hi'), keys)
  const args = {
    clusterId,
    subclusterId: keys.publicKey,
    usr3: Buffer.from(streamTo, 'hex'),
    usr4: Buffer.from(
      (await Encryption.createId('from-STRM-R')).toString(),
      'hex'
    )
  }
  const packets = await peer._message2packets(PacketStream, sealed, args)

  let relayed = false
  peer.send = async (buf, port, address) => {
    const p = Packet.decode(buf)
    if (
      p.type === PacketStream.type &&
      port === 23456 &&
      address === '127.0.0.2'
    ) {
      relayed = true
    }
  }

  for (const p of packets) {
    const buf = await Packet.encode(p)
    await peer._onMessage(buf, { port: 61000, address: '127.0.0.1' })
  }

  t.equal(relayed, true, 'stream relayed to destination peer')
})
