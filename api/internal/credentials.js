/* global DOMException */
import ipc from '../ipc.js'
import location from '../location.js'
import os from '../os.js'

const SUPPORTED_TRANSPORTS = new Set(['sms'])
const nativeCredentials = globalThis.navigator?.credentials ?? null
const nativeGet =
  typeof nativeCredentials?.get === 'function'
    ? nativeCredentials.get.bind(nativeCredentials)
    : null
const isIOS = os.platform() === 'ios'

class OTPCredential {
  #code = ''
  #transports = []
  #origin = ''

  constructor (code, transports, origin) {
    this.type = 'otp'
    this.#code = code
    this.#transports = Array.isArray(transports) ? transports.slice() : []
    this.#origin = origin ?? location.origin
    Object.defineProperties(this, {
      code: {
        enumerable: true,
        configurable: false,
        get: () => this.#code
      },
      transports: {
        enumerable: true,
        configurable: false,
        get: () => this.#transports.slice()
      },
      origin: {
        enumerable: true,
        configurable: false,
        get: () => this.#origin
      }
    })
  }

  toJSON () {
    return {
      type: this.type,
      code: this.code,
      transports: this.transports,
      origin: this.origin
    }
  }
}

function toDOMException (message, name = 'AbortError') {
  try {
    return new DOMException(message, name)
  } catch {
    const error = new Error(message)
    error.name = name
    return error
  }
}

function normalizeTransports (input) {
  if (!input) return ['sms']
  if (Array.isArray(input)) return input.map(String)
  return [String(input)]
}

async function get (options, request = ipc.request) {
  if (nativeGet && isIOS) {
    return nativeGet(options)
  }

  if (arguments.length === 0) {
    throw new TypeError(
      'Failed to execute \u2018get\u2019 on \u2018CredentialsContainer\u2019: 1 argument required, but only 0 present.'
    )
  }

  if (typeof options !== 'object' || options === null) {
    throw new TypeError(
      'Failed to execute \u2018get\u2019 on \u2018CredentialsContainer\u2019: parameter 1 (\u2018options\u2019) is not an object'
    )
  }

  const { otp = null, signal = undefined, mediation } = options

  if (mediation && mediation !== 'optional') {
    throw toDOMException(
      'Only optional mediation is supported',
      'NotSupportedError'
    )
  }

  if (!otp || typeof otp !== 'object') {
    throw toDOMException(
      'Web OTP requests must include an { otp } option',
      'NotSupportedError'
    )
  }

  const transports = normalizeTransports(otp.transport)
  if (
    !transports.some((value) =>
      SUPPORTED_TRANSPORTS.has(String(value).toLowerCase())
    )
  ) {
    throw toDOMException(
      'Only the \u201csms\u201d transport is supported',
      'NotSupportedError'
    )
  }

  let timeout = otp.timeout ?? options.timeout ?? 60000
  if (!Number.isFinite(timeout) || timeout <= 0) {
    timeout = 60000
  }

  const payload = {
    origin: location.origin,
    transport: transports.join(',')
  }

  if (typeof otp.hint === 'string' && otp.hint.length > 0) {
    payload.hint = otp.hint
  }

  if (timeout > 0) {
    payload.timeout = String(Math.floor(timeout))
  }

  const requestOptions = { signal }
  if (timeout > 0) {
    requestOptions.timeout = timeout
  }

  const result = await request(
    'otp.credentials.get',
    payload,
    requestOptions
  )

  if (result.err) {
    throw result.err
  }

  const envelope = result?.data ?? {}
  const data = envelope.data ?? envelope
  const code = data?.code

  if (typeof code !== 'string' || code.length === 0) {
    throw toDOMException(
      'The OTP credential response did not include a code',
      'DataError'
    )
  }

  const responseTransports = Array.isArray(data?.transports)
    ? data.transports
    : transports

  const origin =
    typeof data?.origin === 'string' && data.origin.length > 0
      ? data.origin
      : location.origin

  return new OTPCredential(code, responseTransports, origin)
}

if (typeof globalThis.OTPCredential !== 'function') {
  Object.defineProperty(globalThis, 'OTPCredential', {
    configurable: true,
    enumerable: false,
    writable: true,
    value: OTPCredential
  })
}

export default {
  get
}
