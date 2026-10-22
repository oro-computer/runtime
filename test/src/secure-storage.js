import test from 'oro:test'
import { randomBytes } from 'oro:crypto'
import { Buffer } from 'oro:buffer'
import { setItem, getItem, removeItem, clear, keys } from 'oro:secure-storage'

const SKIP_PATTERNS = [
  'backend not implemented',
  'backend unavailable',
  'android runtime unavailable',
  'application unavailable',
  'jni environment unavailable'
]

function randomId () {
  return randomBytes(16).toString('hex')
}

function makeScope () {
  return `oro://${randomId()}`
}

function shouldSkip (err) {
  const message = String(err?.message || err || '').toLowerCase()
  return SKIP_PATTERNS.some((pattern) => message.includes(pattern))
}

test('secure-storage: string roundtrip', async (t) => {
  const scope = makeScope()
  const key = `key-${randomId()}`
  const value = `value-${Date.now()}-\u0000-🙂-\\u0000-\n"`

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

test('secure-storage: literal escapes, empty strings and missing records', async (t) => {
  const scope = makeScope()
  const key = `percent-${randomId()}-%2F`
  const values = [
    'public%2Fdata', '%20', '%2520', '%2B', '%00', '%25', '%', '%2', '%GG',
    '{"url":"https://public.example/a%2Fb?q=%2520"}', 'a+b c', '🙂\u0000\n', ''
  ]

  try {
    t.equal(await getItem(key, { scope }), null, 'missing record returns null')
    for (const value of values) {
      await setItem(key, value, { scope, encoding: 'utf8' })
      const encoded = Buffer.from(value, 'utf8').toString('base64')
      for (let read = 0; read < 2; read++) {
        t.equal(await getItem(key, { scope }), value, 'UTF-8 read preserves exact input')
        t.equal(await getItem(key, { scope, encoding: 'base64' }), encoded, 'base64 read preserves stored bytes')
      }
      await setItem(key, encoded, { scope, encoding: 'base64' })
      t.equal(await getItem(key, { scope }), value, 'base64 write preserves exact UTF-8 input')
      await setItem(key, Buffer.from(value, 'utf8').toString('hex'), { scope, encoding: 'hex' })
      t.equal(await getItem(key, { scope }), value, 'hex write preserves exact UTF-8 input')
    }
    await removeItem(key, { scope })
    t.equal(await getItem(key, { scope, encoding: 'buffer' }), null, 'removed record returns null')
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

test('secure-storage: binary data and key listing', async (t) => {
  const scope = makeScope()
  const firstKey = `binary-${randomId()}`
  const secondKey = `binary-${randomId()}`
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
