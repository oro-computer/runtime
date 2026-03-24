import { test } from 'oro:test'
import ipc from 'oro:ipc'
import credentials from 'oro:internal/credentials'

const originalRequest = ipc.request

function restore () {
  ipc.request = originalRequest
}

test('credentials.get returns OTPCredential with sms transport', async (t) => {
  const calls = []
  ipc.request = async (command, payload, options) => {
    calls.push({ command, payload, options })
    return {
      data: {
        data: {
          type: 'otp',
          code: '123456',
          transports: ['sms'],
          origin: 'https://example.com'
        }
      }
    }
  }

  try {
    const controller = new AbortController()
    const result = await credentials.get({
      otp: { transport: ['sms'], hint: 'example' },
      timeout: 1234,
      signal: controller.signal
    })

    t.equal(result.type, 'otp', 'returns otp credential type')
    t.equal(result.code, '123456', 'returns expected code')
    t.same(result.transports, ['sms'], 'retains transports')
    t.equal(result.origin, 'https://example.com', 'origin preserved')

    t.equal(calls.length, 1, 'ipc.request invoked once')
    t.equal(calls[0].command, 'otp.credentials.get', 'command matches')
    t.equal(
      calls[0].payload.origin,
      globalThis.location.origin,
      'origin forwarded'
    )
    t.equal(
      calls[0].payload.transport,
      'sms',
      'transport normalised to comma-separated string'
    )
    t.ok(
      calls[0].options.signal === controller.signal,
      'signal forwarded to ipc.request'
    )
    t.equal(calls[0].options.timeout, 1234, 'timeout forwarded to ipc.request')
  } finally {
    restore()
  }
})

test('credentials.get rethrows DOMException when backend returns err', async (t) => {
  const error = new DOMException('denied', 'NotAllowedError')
  ipc.request = async () => ({ err: error })

  try {
    await t.rejects(
      credentials.get({ otp: { transport: 'sms' } }),
      (err) => err.name === 'NotAllowedError' && err.message === 'denied',
      'propagates DOMException from ipc'
    )
  } finally {
    restore()
  }
})

test('credentials.get validates options object', async (t) => {
  ipc.request = async () => {
    throw new Error('unexpected ipc.request call')
  }

  try {
    await t.rejects(
      () => credentials.get(),
      (err) => err instanceof TypeError,
      'throws TypeError when missing options'
    )

    await t.rejects(
      () => credentials.get({}),
      (err) => err instanceof DOMException && err.name === 'NotSupportedError',
      'throws when otp option missing'
    )
  } finally {
    restore()
  }
})
