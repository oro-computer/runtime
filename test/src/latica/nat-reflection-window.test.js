import test from 'oro:test'
import { Peer, Encryption, NAT } from 'oro:latica/index'
import { PacketPong } from 'oro:latica/packets'

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

test('reflection first responder window is canceled by second response, then natType computed', async (t) => {
  const clusterId = await Encryption.createClusterId('NR-WIN')
  const peerId = await Encryption.createId('peer-NR-WIN')
  const signingKeys = await Encryption.createKeyPair('sig-NR-WIN')
  const peer = new Peer(
    {
      config: {
        clusterId,
        peerId,
        address: '10.0.0.5',
        signingKeys,
        natType: NAT.UNRESTRICTED,
        keepalive: 1000
      }
    },
    createDgramStub()
  )

  // capture timeouts
  const timeouts = []
  peer._setTimeout = (fn) => {
    timeouts.push(fn)
    return timeouts.length - 1
  }
  peer._clearTimeout = (id) => {
    if (id >= 0) timeouts[id] = null
  }

  // two reflection pongs, same port (endpoint independent)
  peer.reflectionId = 'feedface000001'
  peer.reflectionStage = 2

  const mkPong = async () =>
    new PacketPong({
      message: {
        requesterPeerId: peerId.toString(),
        responderPeerId: (await Encryption.createId('r')).toString(),
        isReflection: true,
        reflectionId: peer.reflectionId,
        port: 55000,
        address: '10.0.0.5'
      }
    })
  const p1 = await mkPong()
  await peer._onPong(p1, 55000, '10.0.0.5')
  t.ok(!!peer.reflectionFirstResponder, 'first responder set')

  const p2 = await mkPong()
  await peer._onPong(p2, 55000, '10.0.0.5')

  // run any pending timeouts (natType computation)
  for (const fn of timeouts) if (typeof fn === 'function') await fn()

  t.ok(NAT.isValid(peer.natType), 'natType computed after second response')
  t.equal(peer.reflectionFirstResponder, null, 'first responder cleared')
})
