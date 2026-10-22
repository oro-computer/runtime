import test from 'oro:test'

import webShare from 'oro:internal/web-share'
import os from 'oro:os'
import { NotAllowedError, NotSupportedError } from 'oro:errors'
import { validateFiles, MAX_SHARE_BYTES } from 'oro:internal/share-files'

const { canShare, share, normalizeShareData, platformSupportsShare } = webShare

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

test('web share fallback validates platform capabilities', (t) => {
  t.equal(canShare({}), false, 'empty input cannot be shared')

  const expected = supported
  t.equal(
    canShare({ text: 'hello world' }),
    expected,
    'text-only payload matches platform capability'
  )
  t.equal(
    canShare({ title: 'hello', url: 'https://example.com' }),
    expected,
    'url payload matches platform capability'
  )

  if (typeof File === 'function') {
    const file = new File(['hello'], 'hello.txt', { type: 'text/plain' })
    t.equal(
      canShare({ files: [file] }),
      os.platform() === 'android',
      'files match Android capability'
    )
  } else {
    t.equal(
      canShare({ files: ['placeholder'] }),
      false,
      'files array is unsupported without File'
    )
  }
})

test('share file bounds and path validation', (t) => {
  if (typeof File !== 'function') return
  const file = new File(['hello'], 'hello.txt', { type: 'text/plain' })
  t.equal(validateFiles([file]), 5, 'counts actual file bytes')
  t.throws(() => validateFiles([new File([], '.oro-share-created')]), TypeError, 'rejects the reserved expiry marker')
  t.throws(() => validateFiles([new File([], 'é'.repeat(128))]), TypeError, 'bounds UTF-8 filename bytes before copying')
  t.throws(() => validateFiles([file, file]), TypeError, 'rejects duplicate names')
  t.throws(() => validateFiles([new File(['x'], '../secret.txt')]), TypeError, 'rejects traversal')
  t.throws(() => validateFiles([new File(['x'], 'bad\nname')]), TypeError, 'rejects control characters')
  t.throws(() => validateFiles([new File([new Uint8Array(MAX_SHARE_BYTES + 1)], 'large.bin')]), RangeError, 'bounds bytes before reading')
  t.throws(() => validateFiles(Array.from({ length: 17 }, (_, index) => new File([], `${index}.txt`))), TypeError, 'bounds file count')
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

test('normalizeShareData validates input and url', (t) => {
  t.throws(
    () => normalizeShareData('nope'),
    TypeError,
    'non-object input throws TypeError'
  )
  t.throws(
    () => normalizeShareData({ url: 'https://[invalid' }),
    TypeError,
    'invalid url throws TypeError'
  )
})

test('web share fallback rejects when unsupported', async (t) => {
  if (supported) {
    t.ok(
      true,
      'share invocation skipped on supported platform to avoid native UI'
    )
    return
  }

  const ExpectedError = navigator.userActivation?.isActive === false ? NotAllowedError : NotSupportedError
  await t.rejects(
    share({ text: 'hello world' }),
    error => error instanceof ExpectedError,
    'share rejects according to activation and platform policy without opening native UI'
  )
})
