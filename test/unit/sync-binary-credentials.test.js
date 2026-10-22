import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

const ipcSource = readFileSync(new URL('../../api/ipc.js', import.meta.url), 'utf8')
const responseStart = ipcSource.indexOf('function getRequestResponseText (')
const responseEnd = ipcSource.indexOf('function getFileSystemBookmarkName (', responseStart)
const syncStart = ipcSource.indexOf('export function sendSync (')
const syncEnd = ipcSource.indexOf('\n/**', syncStart)
const helpers = ipcSource.slice(ipcSource.indexOf('const fileDescriptorTransports ='), ipcSource.indexOf('/**\n * Sends a synchronous IPC command'))
const functions = helpers + ipcSource.slice(responseStart, responseEnd) + ipcSource.slice(syncStart, syncEnd).replace('export ', '')

test('Android synchronous writes map and send their body before returning through both XHR wrappers', async () => {
  const calls = []
  let mappedBody
  let failure
  class XMLHttpRequest {
    static DONE = 4
    listeners = new Map()
    open (method, uri, async) { this.uri = new URL(uri); this.async = async }
    setRequestHeader () {}
    addEventListener (name, callback) { this.listeners.set(name, callback) }
    send (body) {
      if (failure) throw failure
      calls.push({ command: this.uri.hostname, body: mappedBody })
      assert.equal(body, null)
      this.response = JSON.stringify({ data: { bytesWritten: mappedBody.length } })
      this.responseText = this.response
      this.readyState = XMLHttpRequest.DONE
      this.listeners.get('readystatechange')?.()
    }

    getAllResponseHeaders () { return '' }
  }
  const context = vm.createContext({
    XMLHttpRequest,
    URL,
    TextEncoder,
    Buffer,
    ArrayBuffer,
    window: {},
    document: {},
    location: { origin: 'https://app.example' },
    protocols: { handlers: new Map() },
    primordials: { platform: 'android' },
    __args: { config: {} },
    config: {},
    ConcurrentQueue: class { async push () {} },
    IPCSearchParams: URLSearchParams,
    debug: { enabled: false },
    cache: {},
    errors: {},
    Message: { from: () => ({ command: 'fs.write' }) },
    Headers: { from: () => new Map() },
    Result: { from: value => value },
    isFileSystemBookmark: () => false,
    isBufferLike: ArrayBuffer.isView,
    isPlainObject: value => value !== null && typeof value === 'object' && !ArrayBuffer.isView(value),
    parseJSON (value) { try { return JSON.parse(value) } catch { return null } },
    postMessage (uri, body) { mappedBody = Buffer.from(body); return true }
  })
  const intercept = ipcSource.slice(ipcSource.indexOf('function initializeXHRIntercept ('), ipcSource.indexOf('function getErrorClass ('))
  const init = readFileSync(new URL('../../api/internal/init.js', import.meta.url), 'utf8')
  const initIntercept = init.slice(init.indexOf('// patch `globalThis.XMLHttpRequest`'), init.indexOf('\nimport hooks,'))
  vm.runInContext(`${intercept}; initializeXHRIntercept(); ${initIntercept}`, context)
  const send = vm.runInContext(`${functions}; sendSync`, context)
  for (const body of ['hello world', Buffer.from(Array.from({ length: 256 }, (_, i) => i)), Buffer.alloc(0)]) {
    const result = send('fs.write', { id: 1 }, null, body)
    assert.deepEqual(calls.at(-1)?.body, Buffer.from(body), 'native send completed before sendSync returned')
    assert.equal(result.data.bytesWritten, Buffer.byteLength(body))
  }
  assert.equal(calls.length, 3, 'all native sends complete without yielding to microtasks')
  failure = new Error('Native synchronous send failed')
  assert.throws(() => send('fs.write', { id: 1 }, null, 'x'), error => error === failure)
  failure = null
  const request = new context.XMLHttpRequest()
  request.open('POST', 'ipc://fs.write?id=1', true)
  await request.send('async data')
  assert.equal(calls.at(-1).body.toString(), 'async data')
})

for (const platform of ['android', 'win32', 'linux', 'ios']) {
  test(`synchronous binary IPC preserves all byte values and errors on ${platform}`, () => {
    const payload = Buffer.from(Array.from({ length: 256 }, (_, i) => i))
    const chromium = ['android', 'win32'].includes(platform)
    let body = payload
    let error = null
    class XMLHttpRequest {
      responseType = ''
      headers = new Map()
      open (method, uri, async) {
        assert.equal(async, false)
        if (chromium && this.responseType !== '') throw new Error('Synchronous document XHR must use text')
        this.uri = new URL(uri)
      }

      send () {
        if (error) {
          this.response = JSON.stringify({ source: 'fs.read', err: error })
        } else if (this.uri.searchParams.get('__sync_binary__') === 'true') {
          this.headers.set('x-oro-ipc-encoding', 'base64')
          this.response = body.toString('base64')
        } else {
          this.response = this.responseType === 'arraybuffer' ? body : body.toString('utf8')
        }
        this.responseText = this.response
      }

      getResponseHeader (name) { return this.headers.get(name) }
      getAllResponseHeaders () { return '' }
    }

    const context = vm.createContext({
      XMLHttpRequest,
      document: {},
      Buffer,
      ArrayBuffer,
      primordials: { platform },
      IPCSearchParams: URLSearchParams,
      debug: { enabled: false },
      cache: {},
      Headers: { from: request => request.headers },
      Result: { from: value => value?.err ? value : { data: value } },
      isFileSystemBookmark: () => false,
      isBufferLike: Buffer.isBuffer,
      isPlainObject: value => value !== null && typeof value === 'object' && !Buffer.isBuffer(value),
      parseJSON (value) { try { return JSON.parse(value) } catch { return null } }
    })
    const send = vm.runInContext(`${functions}; sendSync`, context)
    assert.deepEqual(send('fs.read', { id: 1 }, { responseType: 'arraybuffer' }).data, payload)
    body = Buffer.alloc(0)
    assert.deepEqual(send('fs.read', { id: 1 }, { responseType: 'arraybuffer' }).data, body)
    error = { code: 'EBADF', message: 'Bad file descriptor' }
    const result = send('fs.read', { id: 1 }, { responseType: 'arraybuffer' })
    assert.equal(result.err.code, 'EBADF')
  })
}

for (const platform of ['ios', 'android', 'linux', 'win32']) {
  test(`${platform} OTP uses IPC while passkeys retain the native credentials container`, async () => {
    const source = readFileSync(new URL('../../api/internal/credentials.js', import.meta.url), 'utf8')
      .replace(/^import .*\n/gm, '')
      .replace('export default {', 'globalThis.credentials = {')
    const calls = []
    const nativeCalls = []
    const context = vm.createContext({
      DOMException,
      os: { platform: () => platform },
      location: { origin: 'https://app.example' },
      navigator: {
        credentials: {
          async get (options) {
            nativeCalls.push(options)
            if (!options.publicKey) throw new DOMException('Only PublicKeyCredential is supported.', 'NotSupportedError')
            return { type: 'public-key' }
          }
        }
      },
      ipc: {
        async request (command, payload) {
          calls.push({ command, payload })
          return { data: { code: '123456' } }
        }
      }
    })
    vm.runInContext(source, context)
    const otp = await context.credentials.get({ otp: { transport: ['sms'] } })
    assert.equal(otp.code, '123456')
    assert.equal(calls[0].command, 'otp.credentials.get')
    assert.equal(nativeCalls.length, 0)
    const passkey = await context.credentials.get({ publicKey: { challenge: new Uint8Array(32) } })
    assert.equal(passkey.type, 'public-key')
    assert.equal(nativeCalls.length, 1)
    const injected = await context.credentials.get({ otp: { transport: ['sms'] } }, async () => ({ data: { code: '654321' } }))
    assert.equal(injected.code, '654321')
    await assert.rejects(context.credentials.get(null), { name: 'TypeError' })
  })
}

test('filesystem IPC retains descriptor transport across sync, async and late extension availability', async () => {
  const calls = []
  const descriptors = new Map()
  const nativeDescriptors = new Map()
  class XMLHttpRequest {
    static DONE = 4
    open (method, uri, async = true) { this.uri = new URL(uri); this.async = async }
    send (bytes) {
      const id = this.uri.searchParams.get('id')
      calls.push({ transport: 'xhr', command: this.uri.hostname })
      let result
      if (this.uri.hostname === 'fs.open') {
        descriptors.set(id, Buffer.from([0, 128, 255]))
        result = { data: { fd: id } }
      } else if (!descriptors.has(id)) result = { err: { code: 'ENOTOPEN' } }
      else if (this.uri.hostname === 'fs.read') result = descriptors.get(id)
      else {
        descriptors.set(id, Buffer.from(bytes))
        result = { data: { written: bytes.length } }
      }
      this.response = this.responseText = Buffer.isBuffer(result) ? result : JSON.stringify(result)
      this.readyState = XMLHttpRequest.DONE
      if (this.async) queueMicrotask(() => this.onreadystatechange?.())
    }

    getAllResponseHeaders () { return '' }
  }
  const context = vm.createContext({
    XMLHttpRequest,
    document: {},
    Buffer,
    ArrayBuffer,
    queueMicrotask,
    setTimeout,
    clearTimeout,
    ready: async () => {},
    primordials: { platform: 'linux' },
    IPCSearchParams: URLSearchParams,
    debug: { enabled: false },
    cache: {},
    Headers: { from: () => new Map() },
    Result: { from: value => value?.err || value?.data ? value : { data: value } },
    isFileSystemBookmark: () => false,
    isBufferLike: ArrayBuffer.isView,
    isPlainObject: value => value !== null && typeof value === 'object' && !ArrayBuffer.isView(value),
    parseJSON (value) { try { return JSON.parse(value) } catch { return null } },
    __global_ipc_extension_handler (uri, bytes) {
      const request = new URL(uri)
      calls.push({ transport: 'extension', command: request.hostname, bytes })
      const id = request.searchParams.get('id')
      if (request.hostname === 'fs.open') {
        nativeDescriptors.set(id, Buffer.from([1, 128, 254]))
        return JSON.stringify({ data: { fd: id } })
      }
      if (request.hostname === 'fs.read') return nativeDescriptors.get(id)
      return JSON.stringify({ data: { written: bytes?.length } })
    }
  })
  const requestStart = ipcSource.indexOf('export async function request (')
  const requestEnd = ipcSource.indexOf('\n/**', requestStart)
  const requestSource = ipcSource.slice(requestStart, requestEnd).replace('export ', '')
  const { send, request } = vm.runInContext(`${functions}; ${requestSource}; ({ send: sendSync, request })`, context)
  const handler = context.__global_ipc_extension_handler
  context.__global_ipc_extension_handler = undefined
  await request('fs.open', { id: 'early-opened' })
  context.__global_ipc_extension_handler = handler
  assert.deepEqual(send('fs.read', { id: 'early-opened' }, { responseType: 'arraybuffer' }).data, descriptors.get('early-opened'))
  await request('fs.open', { id: 'async-opened' })
  assert.deepEqual(send('fs.read', { id: 'async-opened' }, { responseType: 'arraybuffer' }).data, nativeDescriptors.get('async-opened'))
  assert.equal(calls.at(-1).transport, 'extension')
  send('fs.open', { id: 'sync-opened' })
  assert.deepEqual((await request('fs.read', { id: 'sync-opened' }, { responseType: 'arraybuffer' })).data, descriptors.get('sync-opened'))
  assert.equal(calls.at(-1).transport, 'xhr')
  for (const body of ['public text', Buffer.from([0, 128, 255]), new DataView(Uint8Array.from([1, 2, 3, 4]).buffer, 1, 2)]) {
    const expected = ArrayBuffer.isView(body)
      ? Buffer.from(body.buffer, body.byteOffset, body.byteLength)
      : Buffer.from(body)
    send('fs.write', { id: 'native-opened' }, { useExtensionIPCIfAvailable: true }, body)
    assert.deepEqual(calls.at(-1).bytes, expected)
  }
  assert.equal(send('fs.read', { id: 'missing' }).err.code, 'ENOTOPEN')
  send('fs.read', { id: 'early-opened' }, { useExtensionIPCIfAvailable: false })
  assert.equal(calls.at(-1).transport, 'xhr')
})
