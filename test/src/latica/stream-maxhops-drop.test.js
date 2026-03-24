import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import { Packet, PacketStream } from 'oro:latica/packets'

function createDgramStub () {
  class S {
    on () {}
    removeAllListeners () {}
    setMaxListeners () {}
    bind () {}
    address () {
      return { port: 0 }
    }

    send () {}
    close () {}
    unref () {
      return this
    }
  }
  return {
    createSocket () {
      return new S()
    }
  }
}

test('stream relay stops when hops >= maxHops', async (t) => {
  const clusterId = await Encryption.createClusterId('STRM-HOPS')
  const peerId = await Encryption.createId('peer-STRM-HOPS')
  const signingKeys = await Encryption.createKeyPair('sig-STRM-HOPS')
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

  // destination peer present
  const dest = (await Encryption.createId('dest')).toString()
  peer.peers.push({
    peerId: dest,
    address: '127.0.0.2',
    port: 23030,
    natType: NAT.UNRESTRICTED,
    lastUpdate: Date.now()
  })

  let sent = 0
  peer.send = async () => {
    sent++
  }

  const pkt = new PacketStream({
    clusterId,
    subclusterId: new Uint8Array(32),
    usr3: Buffer.from(dest, 'hex'),
    usr4: Buffer.from(peerId, 'hex'),
    message: Buffer.from('s')
  })
  const buf = await Packet.encode(pkt)
  const p = Packet.decode(buf)
  p.hops = peer.maxHops
  await peer._onStream(p, 1, '127.0.0.1')
  t.equal(sent, 0, 'no relay at maxHops')
})
