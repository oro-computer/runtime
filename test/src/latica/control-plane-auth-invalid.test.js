import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import { Packet, PacketPing } from 'oro:latica/packets'

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

async function makePeer (label = 'X') {
  const clusterId = await Encryption.createClusterId('CLUSTER-INV-' + label)
  const peerId = await Encryption.createId('peer-INV-' + label)
  const signingKeys = await Encryption.createKeyPair('sig-INV-' + label)
  const peer = new Peer(
    {
      config: {
        clusterId,
        peerId,
        address: '127.0.0.1',
        signingKeys,
        controlPlaneAuth: 'sig',
        natType: NAT.UNRESTRICTED,
        keepalive: 1000
      }
    },
    createDgramStub()
  )
  return { peer, clusterId, signingKeys }
}

test('invalid sig length is rejected', async (t) => {
  const { peer, clusterId, signingKeys } = await makePeer('LEN')
  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: (await Encryption.createId('rq-len')).toString(),
      natType: NAT.UNRESTRICTED
    },
    usr2: Buffer.from(signingKeys.publicKey),
    sig: Buffer.alloc(10) // invalid length
  }
  const data = await Packet.encode(new PacketPing(props))
  await peer._onMessage(data, { port: 51001, address: '127.0.0.1' })
  t.equal(peer.metrics.i[PacketPing.type], 0, 'invalid sig length dropped')
})

test('invalid pubkey length is rejected', async (t) => {
  const { peer, clusterId } = await makePeer('PK')
  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: (await Encryption.createId('rq-pk')).toString(),
      natType: NAT.UNRESTRICTED
    },
    usr2: Buffer.alloc(16), // invalid length
    sig: Buffer.alloc(64) // garbage
  }
  const data = await Packet.encode(new PacketPing(props))
  await peer._onMessage(data, { port: 51002, address: '127.0.0.1' })
  t.equal(peer.metrics.i[PacketPing.type], 0, 'invalid pubkey length dropped')
})

test('tampered message is rejected', async (t) => {
  const { peer, clusterId } = await makePeer('TAMPER')
  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: (await Encryption.createId('rq-tamper')).toString(),
      natType: NAT.UNRESTRICTED,
      uptime: 1
    }
  }
  // sign
  peer._applyControlAuth(PacketPing, props)
  // tamper message after signing
  props.message.uptime = 999
  const data = await Packet.encode(new PacketPing(props))
  await peer._onMessage(data, { port: 51003, address: '127.0.0.1' })
  t.equal(peer.metrics.i[PacketPing.type], 0, 'tampered message dropped')
})
