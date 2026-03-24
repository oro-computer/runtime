import test from 'oro:test'
import { Cache } from 'oro:latica/index'
test('cache summary encode/decode (sha256)', async (t) => {
  const prefix = 'ab'
  const hash = 'a'.repeat(64) // fake 32-byte hex
  const buckets = Array.from({ length: 16 }, () => null)
  buckets[0] = 'b'.repeat(64)
  buckets[15] = 'c'.repeat(64)

  const encoded = Cache.encodeSummary({ prefix, hash, buckets })
  const decoded = Cache.decodeSummary(encoded)

  t.equal(decoded.prefix, prefix, 'prefix preserved')
  t.equal(decoded.hash, hash, 'hash preserved')
  t.equal(decoded.buckets[0], buckets[0], 'bucket[0] preserved')
  t.equal(decoded.buckets[15], buckets[15], 'bucket[15] preserved')
})

test('cache summary hash format validation (sha1 and sha256)', async (t) => {
  t.ok(
    Cache.isValidSummaryHashFormat('a'.repeat(40)),
    'accepts 20-byte (SHA-1) hex'
  )
  t.ok(
    Cache.isValidSummaryHashFormat('a'.repeat(64)),
    'accepts 32-byte (SHA-256) hex'
  )
})
