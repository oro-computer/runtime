import test from 'oro:test'
import { Packet, PacketPublish } from 'oro:latica/packets'

// List of PACKET_SPEC keys in encode order
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

function findFieldOffset (buf, fieldName) {
  let o = 4 // magic bytes
  for (const k of KEYS) {
    if (['type', 'version', 'clock', 'hops', 'index', 'ttl'].includes(k)) {
      // numbers: fixed length
      const sizes = { type: 1, version: 2, clock: 4, hops: 4, index: 4, ttl: 4 }
      o += sizes[k]
      continue
    }
    const size = buf.readUInt16BE(o)
    if (k === fieldName) return o
    o += 2 + size
  }
  return -1
}

test('decode returns null when message size exceeds spec', async (t) => {
  const pkt = new PacketPublish({ message: Buffer.from('x') })
  const buf = await Packet.encode(pkt)
  const off = findFieldOffset(buf, 'message')
  t.ok(off > 0, 'found message field')
  // set length to > 1024
  buf.writeUInt16BE(2048, off)
  const decoded = Packet.decode(buf)
  t.equal(decoded, null, 'oversize message returns null')
})

test('decode returns null when sig size exceeds spec', async (t) => {
  const pkt = new PacketPublish({ message: Buffer.from('y') })
  const buf = await Packet.encode(pkt)
  const off = findFieldOffset(buf, 'sig')
  t.ok(off > 0, 'found sig field')
  // set length to > 64
  buf.writeUInt16BE(200, off)
  const decoded = Packet.decode(buf)
  t.equal(decoded, null, 'oversize sig returns null')
})
