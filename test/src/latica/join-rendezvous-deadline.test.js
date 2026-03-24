import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import { Packet, PacketJoin } from 'oro:latica/packets'

function createDgramStub () {
  class StubSocket {
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
      return new StubSocket()
    }
  }
}

test('expired rendezvous deadline causes join to be dropped (no onJoin)', async (t) => {
  const clusterId = await Encryption.createClusterId('RZ-DL')
  const peerId = await Encryption.createId('peer-RZ-DL')
  const signingKeys = await Encryption.createKeyPair('sig-RZ-DL')
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

  // mark membership
  const cid = Buffer.from(clusterId).toString('base64')
  peer.clusters[cid] = {}

  let joinCalled = false
  peer.onJoin = () => {
    joinCalled = true
  }

  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: (await Encryption.createId('rq')).toString(),
      natType: NAT.UNRESTRICTED,
      address: '127.0.0.1',
      port: 1,
      rendezvousDeadline: Date.now() - 1
    }
  }
  const buf = await Packet.encode(new PacketJoin(props))
  await peer._onMessage(buf, { port: 1, address: '127.0.0.1' })
  t.equal(joinCalled, false, 'onJoin not called for expired rendezvous')
})
