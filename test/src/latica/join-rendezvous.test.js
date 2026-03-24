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

test('rendezvous join sends to rendezvous peer address', async (t) => {
  const clusterId = await Encryption.createClusterId('CLUSTER-RZ')
  const peerId = await Encryption.createId('peer-RZ')
  const signingKeys = await Encryption.createKeyPair('sig-RZ')

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

  const sent = []
  peer.send = (buf, port, address) => {
    sent.push({ port, address, packet: Packet.decode(buf) })
  }

  const rendezvousAddress = '203.0.113.1'
  const rendezvousPort = 45678

  const props = {
    clusterId,
    subclusterId: new Uint8Array(32),
    message: {
      requesterPeerId: (await Encryption.createId('rq-rz')).toString(),
      natType: NAT.UNRESTRICTED,
      address: '198.51.100.2',
      port: 23456,
      rendezvousAddress,
      rendezvousPort,
      rendezvousType: NAT.UNRESTRICTED,
      rendezvousPeerId: (await Encryption.createId('peer-RZ-intro')).toString(),
      rendezvousDeadline: Date.now() + 10000
    }
  }

  const data = await Packet.encode(new PacketJoin(props))
  await peer._onMessage(data, {
    port: props.message.port,
    address: props.message.address
  })

  const hasRendezvousSend = sent.some(
    (s) =>
      s.address === rendezvousAddress &&
      s.port === rendezvousPort &&
      s.packet?.type === PacketJoin.type
  )
  t.equal(hasRendezvousSend, true, 'sent a Join to rendezvous address')
})
