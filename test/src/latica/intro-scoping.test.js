import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import { Packet, PacketJoin } from 'oro:latica/packets'

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

test('intro handler not invoked for non-member cluster', async (t) => {
  const clusterA = await Encryption.createClusterId('CLUSTER-A')
  const clusterB = await Encryption.createClusterId('CLUSTER-B')
  const peerId = await Encryption.createId('peer-X')
  const signingKeys = await Encryption.createKeyPair('sig-X')

  const dgram = createDgramStub()
  const peer = new Peer(
    {
      config: {
        clusterId: clusterA,
        peerId,
        address: '127.0.0.1',
        signingKeys,
        natType: NAT.UNRESTRICTED,
        keepalive: 1000
      }
    },
    dgram
  )

  let introCalled = false
  peer.onIntro = () => {
    introCalled = true
  }

  const props = {
    clusterId: clusterB,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: (await Encryption.createId('peer-Y')).toString(),
      natType: NAT.UNRESTRICTED,
      address: '127.0.0.1',
      port: 43210
    }
  }

  const data = await Packet.encode(new PacketJoin(props))
  await peer._onMessage(data, { port: 43210, address: '127.0.0.1' })
  t.equal(introCalled, false, 'onIntro not called for non-member cluster')
})
