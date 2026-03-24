import test from 'oro:test'
import { Packet } from 'oro:latica/packets'

function rand (n) {
  return Math.floor(Math.random() * n)
}

test('Packet.decode is robust to random buffers', async (t) => {
  for (let i = 0; i < 200; i++) {
    const len = rand(4096)
    const buf = Buffer.alloc(len)
    for (let j = 0; j < len; j++) buf[j] = rand(256)
    try {
      const p = Packet.decode(buf)
      t.ok(p === null || typeof p === 'object', 'decode returns null or object')
    } catch {
      t.fail('decode should not throw')
    }
  }
})

test('Packet.decode handles truncated header safely', async (t) => {
  const buf = Buffer.from([0x03, 0x05, 0x0b]) // missing last magic byte
  try {
    const p = Packet.decode(buf)
    t.equal(p, null, 'truncated header returns null')
  } catch {
    t.fail('decode should not throw on truncated header')
  }
})
