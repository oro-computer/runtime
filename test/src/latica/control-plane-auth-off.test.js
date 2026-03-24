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

test('unsigned ping accepted when control-plane auth off (default)', async (t) => {
  const clusterId = await Encryption.createClusterId('CLUSTER-CP-OFF')
  const peerId = await Encryption.createId('peer-CP-OFF')
  const signingKeys = await Encryption.createKeyPair('sig-CP-OFF')

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

  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: (await Encryption.createId('rq-off')).toString(),
      natType: NAT.UNRESTRICTED
    }
  }
  const data = await Packet.encode(new PacketPing(props))
  await peer._onMessage(data, { port: 55555, address: '127.0.0.1' })
  t.equal(
    peer.metrics.i[PacketPing.type],
    1,
    'unsigned ping accepted when flag is off'
  )
})
