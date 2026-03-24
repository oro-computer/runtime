import test from 'oro:test'

import webShare from 'oro:internal/web-share'

const { normalizeShareData, platformSupportsShare } = webShare

const supported = platformSupportsShare()

test('navigator.share polyfill exposes methods', (t) => {
  t.equal(
    typeof globalThis.navigator.share,
    'function',
    'navigator.share is a function'
  )
  t.equal(
    typeof globalThis.navigator.canShare,
    'function',
    'navigator.canShare is a function'
  )
})

test('navigator.canShare validation', (t) => {
  t.throws(
    () => navigator.canShare('nope'),
    TypeError,
    'invalid input throws TypeError'
  )

  const expected = supported
  t.equal(
    navigator.canShare({ text: 'hello world' }),
    expected,
    'text-only payload matches platform capability'
  )
  t.equal(
    navigator.canShare({ title: 'hello', url: 'https://example.com' }),
    expected,
    'url payload matches platform capability'
  )

  if (typeof File === 'function') {
    const file = new File(['hello'], 'hello.txt', { type: 'text/plain' })
    t.equal(
      navigator.canShare({ files: [file] }),
      false,
      'files are reported unsupported'
    )
  } else {
    t.equal(
      navigator.canShare({ files: ['placeholder'] }),
      false,
      'files array is unsupported without File'
    )
  }
})

test('normalizeShareData handles valid input', (t) => {
  const payload = {
    title: 'Example',
    text: 'Body'
  }

  if (globalThis.location) {
    payload.url = '/relative'
  }

  const normalized = normalizeShareData(payload)

  t.equal(normalized.title, 'Example', 'title normalized')
  t.equal(normalized.text, 'Body', 'text normalized')
  if (payload.url) {
    t.equal(
      normalized.url,
      new URL('/relative', globalThis.location.href).href,
      'url normalized against location'
    )
  } else {
    t.equal(
      normalized.url,
      undefined,
      'url remains undefined without base location'
    )
  }
})

test('normalizeShareData validates url', (t) => {
  t.throws(
    () => normalizeShareData({ url: '::::' }),
    TypeError,
    'invalid url throws TypeError'
  )
})

test('navigator.share rejects when unsupported', async (t) => {
  if (supported) {
    t.ok(
      true,
      'share invocation skipped on supported platform to avoid native UI'
    )
    return
  }

  await t.rejects(
    navigator.share({ text: 'hello world' }),
    /NotSupportedError/,
    'share rejects with NotSupportedError on unsupported platforms'
  )
})
