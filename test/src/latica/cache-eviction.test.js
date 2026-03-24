import test from 'oro:test'
import { Cache } from 'oro:latica/cache'
import { Packet, PacketPublish } from 'oro:latica/packets'

async function makePacket (msg) {
  const p = new PacketPublish({ message: Buffer.from(String(msg)) })
  const buf = await Packet.encode(p)
  return Packet.decode(buf)
}

test('cache evicts oldest entry first', async (t) => {
  const cache = new Cache()
  cache.maxSize = 2

  const p1 = await makePacket('one')
  const k1 = p1.packetId.toString('hex')
  cache.insert(k1, p1)

  const p2 = await makePacket('two')
  const k2 = p2.packetId.toString('hex')
  cache.insert(k2, p2)

  // mark first packet as oldest
  cache.data.get(k1).timestamp = Date.now() - 100000

  const p3 = await makePacket('three')
  const k3 = p3.packetId.toString('hex')
  cache.insert(k3, p3)

  t.equal(cache.has(k1), false, 'oldest evicted')
  t.equal(cache.has(k2), true, 'second remains')
  t.equal(cache.has(k3), true, 'newest remains')
})
