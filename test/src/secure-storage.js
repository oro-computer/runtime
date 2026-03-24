import test from 'oro:test'
import { randomUUID } from 'oro:crypto'
import { setItem, getItem, removeItem, clear, keys } from 'oro:secure-storage'

const SKIP_PATTERNS = [
  'backend not implemented',
  'backend unavailable',
  'android runtime unavailable',
  'application unavailable',
  'jni environment unavailable'
]

function makeScope () {
  return `oro://${randomUUID()}`
}

function shouldSkip (err) {
  const message = String(err?.message || err || '').toLowerCase()
  return SKIP_PATTERNS.some((pattern) => message.includes(pattern))
}

test('secure-storage: string roundtrip', async (t) => {
  const scope = makeScope()
  const key = `key-${randomUUID()}`
  const value = `value-${Date.now()}`

  try {
    await setItem(key, value, { scope })
    const resolved = await getItem(key, { scope })
    t.equal(resolved, value, 'retrieves stored string')
  } catch (err) {
    if (shouldSkip(err)) {
      t.skip(err.message || 'secure storage backend unavailable')
      return
    }
    throw err
  } finally {
    try {
      await removeItem(key, { scope })
    } catch {}
  }
})

test('secure-storage: binary data and key listing', async (t) => {
  const scope = makeScope()
  const firstKey = `binary-${randomUUID()}`
  const secondKey = `binary-${randomUUID()}`
  const firstValue = new Uint8Array([1, 2, 3, 4])
  const secondValue = new Uint8Array([9, 8, 7])

  try {
    await setItem(firstKey, firstValue, { scope })
    await setItem(secondKey, secondValue, { scope })

    const fetched = await getItem(secondKey, { scope, encoding: 'buffer' })
    t.ok(
      fetched instanceof Uint8Array,
      'returns Uint8Array when encoding=buffer'
    )
    t.equal(fetched.length, secondValue.length, 'buffer length preserved')
    for (let i = 0; i < fetched.length; i++) {
      t.equal(fetched[i], secondValue[i], `byte ${i} matches`)
    }

    const list = await keys({ scope })
    t.ok(Array.isArray(list), 'keys() returns an array')
    t.ok(list.includes(firstKey), 'keys include first key')
    t.ok(list.includes(secondKey), 'keys include second key')
  } catch (err) {
    if (shouldSkip(err)) {
      t.skip(err.message || 'secure storage backend unavailable')
      return
    }
    throw err
  } finally {
    try {
      await clear({ scope })
    } catch {}
  }
})
