import test from 'oro:test'
import { Packet, PacketPublish } from 'oro:latica/packets'

const KEYS = [
  'type',
  'version',
  'clock',
  'hops',
  'index',
  'ttl',
  'clusterId',
  'subclusterId',
  'previousId',
  'packetId',
  'nextId',
  'usr1',
  'usr2',
  'usr3',
  'usr4',
  'message',
  'sig'
]

function fieldOffset (buf, name) {
  let o = 4
  for (const k of KEYS) {
    if (['type', 'version', 'clock', 'hops', 'index', 'ttl'].includes(k)) {
      const sizes = { type: 1, version: 2, clock: 4, hops: 4, index: 4, ttl: 4 }
      o += sizes[k]
      continue
    }
    const start = o
    const size = buf.readUInt16BE(o)
    if (k === name) return start
    o += 2 + size
  }
  return -1
}

test('decode returns null when usr1 oversize', async (t) => {
  const pkt = new PacketPublish({ message: Buffer.from('u1') })
  const buf = await Packet.encode(pkt)
  const off = fieldOffset(buf, 'usr1')
  buf.writeUInt16BE(100, off)
  const decoded = Packet.decode(buf)
  t.equal(decoded, null, 'usr1 oversize → null')
})

test('decode returns null when usr2 oversize', async (t) => {
  const pkt = new PacketPublish({ message: Buffer.from('u2') })
  const buf = await Packet.encode(pkt)
  const off = fieldOffset(buf, 'usr2')
  buf.writeUInt16BE(200, off)
  const decoded = Packet.decode(buf)
  t.equal(decoded, null, 'usr2 oversize → null')
})
