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

test('signed join triggers onJoin for same cluster', async (t) => {
  const clusterId = await Encryption.createClusterId('CLUSTER-JOIN-A')
  const peerId = await Encryption.createId('peer-JOIN-A')
  const signingKeys = await Encryption.createKeyPair('sig-JOIN-A')

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

  // make peer a member of the cluster (so _onJoin emits onJoin)
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
      requesterPeerId: (await Encryption.createId('rq-join')).toString(),
      natType: NAT.UNRESTRICTED,
      address: '127.0.0.1',
      port: 23456
    }
  }
  peer._applyControlAuth(PacketJoin, props)
  const data = await Packet.encode(new PacketJoin(props))
  await peer._onMessage(data, { port: 23456, address: '127.0.0.1' })

  t.equal(peer.metrics.i[PacketJoin.type] > 0, true, 'join processed')
  t.equal(joinCalled, true, 'onJoin called for same cluster')
})

test('signed join for other cluster does not trigger onIntro', async (t) => {
  const clusterA = await Encryption.createClusterId('CLUSTER-JOIN-B-A')
  const clusterB = await Encryption.createClusterId('CLUSTER-JOIN-B-B')
  const peerId = await Encryption.createId('peer-JOIN-B')
  const signingKeys = await Encryption.createKeyPair('sig-JOIN-B')

  const peer = new Peer(
    {
      config: {
        clusterId: clusterA,
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

  // ensure membership only in clusterA
  const cidA = Buffer.from(clusterA).toString('base64')
  peer.clusters[cidA] = {}

  let introCalled = false
  peer.onIntro = () => {
    introCalled = true
  }

  const props = {
    clusterId: clusterB,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: (await Encryption.createId('rq-join-b')).toString(),
      natType: NAT.UNRESTRICTED,
      address: '127.0.0.1',
      port: 34567
    }
  }
  peer._applyControlAuth(PacketJoin, props)
  const data = await Packet.encode(new PacketJoin(props))
  await peer._onMessage(data, { port: 34567, address: '127.0.0.1' })

  t.equal(introCalled, false, 'onIntro not called for non-member cluster')
})
