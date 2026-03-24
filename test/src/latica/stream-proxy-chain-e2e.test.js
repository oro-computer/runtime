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

test('A -> B -> C proxy chain forwards stream to final destination', async (t) => {
  const clusterId = await Encryption.createClusterId('STRM-PROXY-CHAIN')
  const peerIdA = await Encryption.createId('peer-A')
  const peerIdB = await Encryption.createId('peer-B')
  const peerIdC = await Encryption.createId('peer-C')
  const signingKeysA = await Encryption.createKeyPair('sig-A')
  const signingKeysB = await Encryption.createKeyPair('sig-B')
  const signingKeysC = await Encryption.createKeyPair('sig-C')
  const sharedKey = await Encryption.createSharedKey('sub-proxy-chain')

  const dgram = createDgramStub()
  const peerA = new Peer(
    {
      config: {
        clusterId,
        peerId: peerIdA,
        address: '127.0.0.1',
        signingKeys: signingKeysA,
        natType: NAT.UNRESTRICTED,
        keepalive: 1000
      }
    },
    dgram
  )
  const peerB = new Peer(
    {
      config: {
        clusterId,
        peerId: peerIdB,
        address: '127.0.0.2',
        signingKeys: signingKeysB,
        natType: NAT.UNRESTRICTED,
        keepalive: 1000
      }
    },
    dgram
  )
  const peerC = new Peer(
    {
      config: {
        clusterId,
        peerId: peerIdC,
        address: '127.0.0.3',
        signingKeys: signingKeysC,
        natType: NAT.UNRESTRICTED,
        keepalive: 1000
      }
    },
    dgram
  )

  // A knows B; B knows C
  peerA.peers.push({
    peerId: peerIdB.toString(),
    address: '127.0.0.2',
    port: 22000,
    natType: NAT.UNRESTRICTED
  })
  peerB.peers.push({
    peerId: peerIdC.toString(),
    address: '127.0.0.3',
    port: 23000,
    natType: NAT.UNRESTRICTED
  })

  // C owns subcluster
  const keys = await Encryption.createKeyPair(sharedKey)
  peerC.encryption.add(keys.publicKey, keys.privateKey)

  // Wire A->B, B->C send paths
  peerA.send = async (buf, port, address) => {
    await peerB._onMessage(buf, { port, address })
  }
  peerB.send = async (buf, port, address) => {
    await peerC._onMessage(buf, { port, address })
  }

  const payload = Buffer.from('CHAIN')
  const sealed = peerA.encryption.seal(payload, keys)
  const args = {
    clusterId,
    subclusterId: keys.publicKey,
    usr3: Buffer.from(peerIdC, 'hex'),
    usr4: Buffer.from(peerIdA, 'hex')
  }
  const packets = await peerA._message2packets(PacketStream, sealed, args)

  let got = ''
  peerC.onStream = async (p) => {
    const opened = await peerC.open(
      p.message,
      Buffer.from(p.subclusterId).toString('base64')
    )
    got = Buffer.from(opened).toString('utf8')
  }

  for (const p of packets) {
    const buf = await Packet.encode(p)
    await peerA._onMessage(buf, { port: 21000, address: '127.0.0.4' })
  }

  t.equal(
    got,
    'CHAIN',
    'destination received composed stream through proxy chain'
  )
})
