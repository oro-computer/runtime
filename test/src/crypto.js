import { test } from 'oro:test'
import crypto from 'oro:crypto'
import Buffer from 'oro:buffer'

test('crypto', async (t) => {
  t.equal(crypto.webcrypto, window.crypto, 'crypto.webcrypto is window.crypto')
  const randomValues = crypto.getRandomValues(new Uint32Array(10))
  t.equal(
    randomValues.length,
    10,
    'crypto.getRandomValues returns an array of the correct length'
  )
  t.ok(
    randomValues.some(
      (value) => value !== randomValues[9],
      'crypto.getRandomValues returns an array of random values'
    )
  )
  t.ok(
    randomValues.every((value) => Number.isInteger(value)),
    'crypto.getRandomValues returns an array of integers'
  )

  const backing = new Uint8Array(24)
  const subarray = backing.subarray(4, 16)
  crypto.getRandomValues(subarray)
  t.ok(
    backing.slice(0, 4).every((value) => value === 0),
    'crypto.getRandomValues does not overwrite preceding bytes for subarray views'
  )
  t.ok(
    backing.slice(16).every((value) => value === 0),
    'crypto.getRandomValues does not overwrite trailing bytes for subarray views'
  )

  t.equal(
    crypto.RANDOM_BYTES_QUOTA,
    64 * 1024,
    'crypto.RANDOM_BYTES_QUOTA is 65536'
  )
  t.equal(
    crypto.MAX_RANDOM_BYTES,
    0xffff_ffff_ffff,
    'crypto.MAX_RANDOM_BYTES is 0xFFFF_FFFF_FFFF'
  )
  t.equal(
    crypto.MAX_RANDOM_BYTES_PAGES,
    crypto.MAX_RANDOM_BYTES / crypto.RANDOM_BYTES_QUOTA,
    `crypto.MAX_RANDOM_BYTES_PAGES is ${crypto.MAX_RANDOM_BYTES / crypto.RANDOM_BYTES_QUOTA}`
  )

  const buffer = crypto.randomBytes(10)
  t.equal(
    buffer.length,
    10,
    'crypto.randomBytes returns a buffer of the correct length'
  )
  t.ok(
    buffer.some(
      (value) => value !== buffer[9],
      'crypto.randomBytes returns a buffer of random values'
    )
  )
  t.ok(
    buffer.every((value) => Number.isInteger(value)),
    'crypto.randomBytes returns a buffer of integers'
  )

  const digest = await crypto.createDigest('SHA-256', new Uint8Array(32))
  t.ok(digest instanceof Buffer, 'crypto.createDigest returns a buffer')
  t.equal(
    digest.length,
    32,
    'crypto.createDigest returns a buffer of the correct length'
  )
})

test('crypto.rand64', (t) => {
  const randoms = Array.from({ length: 10 }, (_) => crypto.rand64())
  t.ok(
    randoms.every((b) => typeof b === 'bigint'),
    'crypto.rand64 returns a bigint'
  )
  t.ok(
    randoms.some((b) => b !== randoms[9]),
    'crypto.rand64 returns a different bigint each time'
  )
})

test('crypto.murmur3 typed arrays hash raw bytes', (t) => {
  const bytes = new Uint8Array([0xff, 0x00, 0xfe, 0x10, 0x7f, 0x80, 0x01])
  const slice = bytes.subarray(2, 6)

  function referenceMurmur3 (data, seed = 0) {
    let hash = seed | 0
    const length = data.byteLength
    const bodyLength = length & ~3
    const c1 = 0xcc9e2d51
    const c2 = 0x1b873593
    const r1 = 15
    const r2 = 13
    const m = 5
    const n = 0xe6546b64

    for (let i = 0; i < bodyLength; i += 4) {
      let k =
        (data[i] |
          (data[i + 1] << 8) |
          (data[i + 2] << 16) |
          (data[i + 3] << 24)) >>>
        0

      k = Math.imul(k, c1)
      k = (k << r1) | (k >>> (32 - r1))
      k = Math.imul(k, c2)

      hash ^= k
      hash = (hash << r2) | (hash >>> (32 - r2))
      hash = (Math.imul(hash, m) + n) | 0
    }

    let k1 = 0

    switch (length & 3) {
      case 3:
        k1 ^= data[bodyLength + 2] << 16
      // falls through
      case 2:
        k1 ^= data[bodyLength + 1] << 8
      // falls through
      case 1:
        k1 ^= data[bodyLength]
        k1 = Math.imul(k1, c1)
        k1 = (k1 << r1) | (k1 >>> (32 - r1))
        k1 = Math.imul(k1, c2)
        hash ^= k1
    }

    hash ^= length
    hash ^= hash >>> 16
    hash = Math.imul(hash, 0x85ebca6b)
    hash ^= hash >>> 13
    hash = Math.imul(hash, 0xc2b2ae35)
    hash ^= hash >>> 16
    return hash >>> 0
  }

  t.equal(
    crypto.murmur3(bytes),
    referenceMurmur3(bytes),
    'crypto.murmur3 matches reference for full typed array'
  )
  t.equal(
    crypto.murmur3(slice),
    referenceMurmur3(slice),
    'crypto.murmur3 respects view offsets for typed arrays'
  )
  t.equal(
    crypto.murmur3('abc'),
    1437992374,
    'crypto.murmur3 preserves legacy string hashing'
  )
})
