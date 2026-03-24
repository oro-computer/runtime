import test from 'oro:test'
import { PacketStream } from 'oro:latica/packets'

test('Packet.encode throws ETOOBIG for oversize stream message', async (t) => {
  const p = new PacketStream({ message: Buffer.alloc(2048) })
  let threw = false
  try {
    await (await import('oro:latica/packets')).Packet.encode(p)
  } catch (e) {
    threw = /ETOOBIG/.test(String(e?.message || e))
  }
  t.equal(threw, true, 'encoding oversize PacketStream throws ETOOBIG')
})
