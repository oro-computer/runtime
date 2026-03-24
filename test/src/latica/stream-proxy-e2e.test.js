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

test('proxy forwards stream end-to-end to destination peer', async (t) => {
  const clusterId = await Encryption.createClusterId('STRM-PROXY')
  const signingKeysA = await Encryption.createKeyPair('sig-A')
  const signingKeysB = await Encryption.createKeyPair('sig-B')
  const peerIdA = await Encryption.createId('peer-A')
  const peerIdB = await Encryption.createId('peer-B')
  const sharedKey = await Encryption.createSharedKey('sub-proxy')

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

  // peerA knows destination peerB
  peerA.peers.push({
    peerId: peerIdB.toString(),
    address: '127.0.0.2',
    port: 22222,
    natType: NAT.UNRESTRICTED
  })

  // subcluster keys owned by peerB
  const keys = await Encryption.createKeyPair(sharedKey)
  peerB.encryption.add(keys.publicKey, keys.privateKey)

  // peerA will "send" by calling peerB._onMessage directly (simulated network)
  peerA.send = async (buf, port, address) => {
    await peerB._onMessage(buf, { port, address })
  }

  const payload = Buffer.from('PROXY-STREAM')
  const sealed = peerA.encryption.seal(payload, keys)
  const args = {
    clusterId,
    subclusterId: keys.publicKey,
    usr3: Buffer.from(peerIdB, 'hex'),
    usr4: Buffer.from(peerIdA, 'hex')
  }
  const packets = await peerA._message2packets(PacketStream, sealed, args)

  let got = ''
  peerB.onStream = async (p) => {
    const opened = await peerB.open(
      p.message,
      Buffer.from(p.subclusterId).toString('base64')
    )
    got = Buffer.from(opened).toString('utf8')
  }

  for (const p of packets) {
    const buf = await Packet.encode(p)
    await peerA._onMessage(buf, { port: 11111, address: '127.0.0.3' })
  }

  t.equal(got, 'PROXY-STREAM', 'destination received composed stream via proxy')
})
