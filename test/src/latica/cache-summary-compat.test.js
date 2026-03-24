import test from 'oro:test'
import { Cache } from 'oro:latica/cache'

function hex (byte, len) {
  return Buffer.alloc(len, byte).toString('hex')
}

test('decodeSummary accepts SHA-1 sized frames (20 bytes)', async (t) => {
  // prefix 'a' (1 nibble)
  const plen = 1
  const prefixByte = Buffer.from([0x0a]) // '0a' -> slice(-1) => 'a'
  const header = Buffer.from([plen])
  const sha1Hash = Buffer.alloc(20, 0xaa)
  // one bucket at offset 3 with 20 bytes
  const bucket = Buffer.concat([Buffer.from([3]), Buffer.alloc(20, 0xbb)])
  const frame = Buffer.concat([header, prefixByte, sha1Hash, bucket])

  const { prefix, hash, buckets } = Cache.decodeSummary(frame)
  t.equal(prefix, 'a', 'prefix decoded')
  t.equal(hash, hex(0xaa, 20), 'sha1-sized hash decoded')
  t.equal(buckets[3], hex(0xbb, 20), 'sha1-sized bucket decoded')
})
