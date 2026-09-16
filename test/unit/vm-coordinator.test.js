import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { MessageChannel, MessagePort } from 'node:worker_threads'
import vm from 'node:vm'

const source = readFileSync(new URL('../../api/vm.js', import.meta.url), 'utf8')
const workerSource = readFileSync(new URL('../../api/vm/worker.js', import.meta.url), 'utf8').replace('export {}', '')
const helpers = source.slice(source.indexOf('function waitForContextWorkerReady '), source.indexOf('// A weak mapping of context objects'))
const getWorker = source.slice(source.indexOf('export async function getContextWorker '), source.indexOf('/**\n * Terminates the VM script context window.')).replace('export ', '')
const interfaces = source.slice(source.indexOf('export class ContextWorkerInterface '), source.indexOf('/**\n * Global reserved values')).replaceAll('export ', '')

for (const feature of [undefined, 'true']) {
  test(`Windows VM windows share a coordinator with WebView2 worker flag ${feature}`, async () => {
    const coordinator = vm.runInNewContext(`${workerSource}; new State()`, {
      EventTarget, MessageEvent, MessagePort, console, setTimeout
    })
    const connections = []
    class SharedWorker extends EventTarget {
      ready = Promise.resolve()
      channel = new MessageChannel()
      port = this.channel.port1

      constructor () {
        super()
        connections.push(this)
        coordinator.onConnect({ ports: [this.channel.port2] })
      }
    }

    function createWindow () {
      const window = {}
      const context = vm.createContext({
        window,
        top: window,
        origin: 'oro://computer.oro.runtime.tests',
        location: { pathname: '/oro/vm/index.html' },
        SharedWorker,
        EventTarget,
        MessageChannel,
        process: { env: { COREWEBVIEW2_22_AVAILABLE: feature } },
        os: { platform: () => 'win32' },
        globals: { register () {} },
        gc: { ref () {}, finalizer: Symbol('finalizer') },
        console,
        setTimeout,
        clearTimeout,
        setInterval,
        clearInterval
      })
      return vm.runInContext(`
        let contextWorker = null;
        const kWorkerContextReady = Symbol();
        const VM_WORKER_ACK = 'VM_SHARED_WORKER_ACK';
        const VM_WORKER_PROBE = 'VM_SHARED_WORKER_PROBE';
        const VM_WINDOW_PATH = '/oro/vm/index.html';
        ${interfaces}
        ${helpers}
        ${getWorker}
        getContextWorker;
      `, context)
    }

    let timer
    const connected = []
    try {
      const getClient = createWindow()
      const client = await getClient()
      connected.push(client)
      const realm = await createWindow()()
      connected.push(realm)
      assert.equal(connections.length, 2, 'both native windows connect to the shared coordinator')
      assert.equal(await getClient(), client, 'the connection is reused within a window')
      realm.port.addEventListener('message', event => {
        if (event.data.type === 'script') {
          realm.port.postMessage({ ...event.data, type: 'result', data: 6 })
        }
      })
      const result = new Promise((resolve, reject) => {
        timer = setTimeout(() => reject(new Error('VM windows did not exchange a result')), 2000)
        client.port.addEventListener('message', event => {
          if (event.data.type === 'result') resolve(event.data.data)
        })
      })
      client.port.postMessage({ type: 'client', id: 'script-1' })
      client.port.postMessage({ type: 'script', id: 'script-1', source: '1 + 2 + 3' })
      realm.port.postMessage({ type: 'realm' })
      assert.equal(await result, 6)
    } finally {
      clearTimeout(timer)
      for (const worker of new Set([...connections, ...connected])) {
        worker.channel.port1.close()
        worker.channel.port2.close()
      }
    }
  })
}
