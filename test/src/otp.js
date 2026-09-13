import { test } from 'oro:test'
import credentials from 'oro:internal/credentials'
import location from 'oro:location'

test('credentials.get returns OTPCredential with sms transport', async (t) => {
  const calls = []
  const request = async (command, payload, options) => {
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

  const controller = new AbortController()
  const result = await credentials.get(
    {
      otp: { transport: ['sms'], hint: 'example' },
      timeout: 1234,
      signal: controller.signal
    },
    request
  )

  t.equal(result.type, 'otp', 'returns otp credential type')
  t.equal(result.code, '123456', 'returns expected code')
  t.same(result.transports, ['sms'], 'retains transports')
  t.equal(result.origin, 'https://example.com', 'origin preserved')

  t.equal(calls.length, 1, 'ipc.request invoked once')
  t.equal(calls[0].command, 'otp.credentials.get', 'command matches')
  t.equal(
    calls[0].payload.origin,
    location.origin,
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
})

test('credentials.get rethrows DOMException when backend returns err', async (t) => {
  const error = new DOMException('denied', 'NotAllowedError')

  await t.rejects(
    credentials.get(
      { otp: { transport: 'sms' } },
      async () => ({ err: error })
    ),
    (err) => err.name === 'NotAllowedError' && err.message === 'denied',
    'propagates DOMException from ipc'
  )
})

test('credentials.get validates options object', async (t) => {
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
})
