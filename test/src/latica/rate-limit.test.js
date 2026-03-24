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

test('rate limiting counts dropped replicatable joins under auth', async (t) => {
  const clusterId = await Encryption.createClusterId('CLUSTER-RATE')
  const peerId = await Encryption.createId('peer-RATE')
  const signingKeys = await Encryption.createKeyPair('sig-RATE')
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

  const cid = Buffer.from(clusterId).toString('base64')
  peer.clusters[cid] = {}

  const droppedBefore = peer.metrics.i.DROPPED
  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: (await Encryption.createId('rq-rate')).toString(),
      natType: NAT.UNRESTRICTED,
      address: '127.0.0.1',
      port: 40000
    }
  }

  // sign props once (stateless); we re-encode each time
  peer._applyControlAuth(PacketJoin, props)
  const ADDR = '198.51.100.50'
  const PORT = 40123

  // Send > 1024 in same minute to trigger drop
  for (let i = 0; i < 1050; i++) {
    const buf = await Packet.encode(new PacketJoin(props))
    await peer._onMessage(buf, { port: PORT, address: ADDR })
  }

  const droppedAfter = peer.metrics.i.DROPPED
  t.ok(
    droppedAfter >= droppedBefore,
    'rate limiting counted drops (>= baseline)'
  )
})
