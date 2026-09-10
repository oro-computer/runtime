import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

const ipcSource = readFileSync(new URL('../../api/ipc.js', import.meta.url), 'utf8')
const responseStart = ipcSource.indexOf('function getRequestResponseText (')
const responseEnd = ipcSource.indexOf('function getFileSystemBookmarkName (', responseStart)
const syncStart = ipcSource.indexOf('export function sendSync (')
const syncEnd = ipcSource.indexOf('\n/**', syncStart)
const functions = ipcSource.slice(responseStart, responseEnd) + ipcSource.slice(syncStart, syncEnd).replace('export ', '')

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

test('iOS OTP uses IPC while passkeys retain the native credentials container', async () => {
  const source = readFileSync(new URL('../../api/internal/credentials.js', import.meta.url), 'utf8')
    .replace(/^import .*\n/gm, '')
    .replace('export default {', 'globalThis.credentials = {')
  const calls = []
  const nativeCalls = []
  const context = vm.createContext({
    DOMException,
    os: { platform: () => 'ios' },
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
