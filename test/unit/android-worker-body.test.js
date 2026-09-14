import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

const ipcSource = readFileSync(new URL('../../api/ipc.js', import.meta.url), 'utf8')
const intercept = ipcSource.slice(ipcSource.indexOf('function initializeXHRIntercept ('), ipcSource.indexOf('function getErrorClass ('))
const initSource = readFileSync(new URL('../../api/internal/init.js', import.meta.url), 'utf8')
const relayStart = initSource.indexOf('          const request = data.__runtime_worker_ipc_request')
const relayEnd = initSource.indexOf('\n        } else {', relayStart)
assert.ok(relayStart >= 0 && relayEnd > relayStart)
const relaySource = initSource.slice(relayStart, relayEnd)

test('worker IPC replies unblock module evaluation while application messages wait for initialization', async () => {
  const source = readFileSync(new URL('../../api/internal/worker.js', import.meta.url), 'utf8')
  const start = source.indexOf('export async function onWorkerMessage (')
  const end = source.indexOf('\nexport function addEventListener (', start)
  let initialize
  const events = []
  const context = vm.createContext({
    promise: new Promise(resolve => { initialize = resolve }),
    __args: { index: 71 },
    CustomEvent: class { constructor (type, options) { this.type = type; this.detail = options.detail } },
    MessageEvent: class { constructor (type, options) { this.type = type; this.data = options.data } },
    ipc: { Message: { from: uri => ({ seq: new URL(uri).searchParams.get('seq') }) } },
    dispatchEvent: event => events.push(event)
  })
  const receive = vm.runInContext(`${source.slice(start, end).replace('export ', '')}; onWorkerMessage`, context)
  const application = receive({ type: 'message', data: { hello: true } })
  let stopped = false
  const reply = receive({
    data: { __runtime_worker_ipc_result: { message: 'ipc://buffer.map?seq=R20-99', result: { data: {} } } },
    stopImmediatePropagation () { stopped = true }
  })
  assert.equal(events.length, 1, 'the IPC response arrives even though module evaluation is pending')
  assert.equal(events[0].type, 'resolve-71-R20-99')
  assert.equal(stopped, true, 'internal replies are consumed before other message listeners run')
  await reply
  initialize()
  await application
  assert.equal(events.length, 2)
  assert.deepEqual(events[1].data, { hello: true })
})

test('Android worker XHR bodies keep distinct keys and wait for the document bridge acknowledgement', async () => {
  const buffers = new Map()
  const messages = []
  const results = []
  const errors = []
  let mappingError = null
  let nextId = 0
  const result = value => ({ ...value, toJSON: () => value })
  const context = vm.createContext({
    queueMicrotask,
    window: {},
    reportError: error => errors.push(error),
    ipc: {
      Message: {
        from (uri, bytes) {
          const url = new URL(uri)
          return {
            name: url.hostname,
            bytes,
            rawParams: Object.fromEntries(url.searchParams),
            get: key => url.searchParams.get(key),
            toJSON: () => uri
          }
        }
      },
      postMessage (uri, bytes) {
        if (mappingError) throw mappingError
        const key = new URL(uri).searchParams.get('seq')
        assert.ok(!buffers.has(key), 'concurrent workers never overwrite a mapped body')
        buffers.set(key, Buffer.from(bytes))
        return true
      },
      async send (name, params) {
        assert.equal(params.seq, undefined, 'ordinary requests allocate a document sequence')
        return result({ data: name })
      },
      Result: { from: (value, err) => result(err ? { err } : value) },
      findMessageTransfers () {}
    }
  })
  const createRelay = vm.runInContext(`(postMessage => function (data) { ${relaySource} })`, context)
  const makeWorker = () => {
    class XMLHttpRequest {
      headers = new Map()
      open () {}
      setRequestHeader (name, value) { this.headers.set(name, value) }
      send (body) {
        assert.equal(body, null)
        const key = this.headers.get('runtime-xhr-seq')
        assert.ok(buffers.has(key), 'the request sees its bytes before native send')
        results.push(buffers.get(key))
        buffers.delete(key)
      }
    }
    const worker = vm.createContext({
      XMLHttpRequest,
      URL,
      TextEncoder,
      self: {},
      location: { origin: 'https://app.example' },
      protocols: { handlers: new Map() },
      primordials: { platform: 'android' },
      rand64: () => ++nextId,
      postMessage () { assert.fail('worker writes must wait for an acknowledgement') },
      send (name, params, options) {
        return new Promise(resolve => {
          const relay = createRelay(data => resolve(data.__runtime_worker_ipc_result.result))
          messages.push(() => relay.call({
            postMessage: () => assert.fail('application messages remain queued during module evaluation')
          }, {
            __runtime_worker_ipc_request: {
              message: `ipc://${name}?${new URLSearchParams(params)}`,
              bytes: options.bytes
            }
          }))
        })
      }
    })
    vm.runInContext(`${intercept}; initializeXHRIntercept()`, worker)
    return new XMLHttpRequest()
  }

  const first = makeWorker()
  const second = makeWorker()
  // Workers independently allocate the same ordinary IPC sequence number.
  first.open('POST', 'ipc://serviceWorker.fetch.response.write?seq=R20', true)
  second.open('POST', 'ipc://serviceWorker.fetch.response.write?seq=R20', true)
  const pending = [first.send('{"ok":true}'), second.send(new Uint8Array([0, 127, 255]))]
  await Promise.resolve()
  assert.equal(results.length, 0, 'native send waits while the document relay is paused')
  assert.notEqual(first.headers.get('runtime-xhr-seq'), second.headers.get('runtime-xhr-seq'))
  // Deliver in reverse order to exercise correlation across concurrent workers.
  messages.reverse().forEach(deliver => deliver())
  await Promise.all(pending)
  assert.deepEqual(results, [Buffer.from([0, 127, 255]), Buffer.from('{"ok":true}')])
  assert.equal(buffers.size, 0, 'both body mappings were consumed')

  const ordinary = await new Promise(resolve => createRelay(
    data => resolve(data.__runtime_worker_ipc_result.result)
  )({ __runtime_worker_ipc_request: { message: 'ipc://os.platform?seq=R20' } }))
  assert.equal(ordinary.data, 'os.platform')

  // A nested worker must relay to its parent and defer its acknowledgement.
  const directSend = context.ipc.send
  const directMap = context.ipc.postMessage
  let completeMapping
  context.window = undefined
  context.ipc.postMessage = () => assert.fail('only documents can map Android bytes directly')
  context.ipc.send = (name, params, options) => {
    assert.equal(name, 'buffer.map')
    assert.equal(params.seq, 'R20-nested')
    assert.deepEqual(options.bytes, Buffer.from('nested'))
    return new Promise(resolve => { completeMapping = resolve })
  }
  let acknowledged = false
  const nested = new Promise(resolve => createRelay(data => {
    acknowledged = true
    resolve(data)
  })({ __runtime_worker_ipc_request: { message: 'ipc://buffer.map?seq=R20-nested', bytes: Buffer.from('nested') } }))
  await Promise.resolve()
  assert.equal(acknowledged, false)
  completeMapping(result({ data: {} }))
  await nested
  context.window = {}
  context.ipc.send = directSend
  context.ipc.postMessage = directMap

  mappingError = new Error('Android bridge could not map the request body')
  const failed = makeWorker()
  failed.open('POST', 'ipc://fs.write?seq=R20', true)
  const rejected = assert.rejects(failed.send('body'), error => error === mappingError)
  messages.at(-1)()
  await rejected
  assert.equal(results.length, 2, 'mapping failures never send an empty native request')
  assert.deepEqual(errors, [])
})
