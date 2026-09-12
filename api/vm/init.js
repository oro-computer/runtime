import application from '../application.js'
import serialize from '../internal/serialize.js'
import { postWindowMessage } from '../internal/post-message.js'
import ipc from '../ipc.js'
import * as vm from '../vm.js'
import hooks from '../hooks.js'

function serializeWindowMessage (message) {
  const transfers = new Set()
  const serialized = serialize(ipc.findIPCMessageTransfers(transfers, message))

  for (const transfer of transfers) {
    if (transfer instanceof ipc.IPCMessagePort) {
      ipc.IPCMessagePort.transfer(transfer)
    }
  }

  return serialized
}

function createTransferredError (error) {
  return {
    name: error?.name ?? 'Error',
    type: error?.constructor?.name ?? 'Error',
    message: error?.message ?? String(error ?? ''),
    stack: error?.stack
  }
}

class World extends EventTarget {
  ready = null
  frame = null
  state = null
  id = null

  constructor (state, id) {
    super()
    this.state = state
    this.id = id
    this.frame = createWorld({ id })
    this.ready = new Promise((resolve, reject) => {
      let timeout = null

      const cleanup = () => {
        clearTimeout(timeout)
        globalThis.removeEventListener('message', onReady)
        this.frame.removeEventListener('error', onError)
      }

      const finish = (error = null) => {
        cleanup()
        if (error) {
          reject(error)
        } else {
          resolve()
        }
      }

      const onReady = (event) => {
        if (
          event.source === this.frame.contentWindow &&
          event.origin === globalThis.location.origin &&
          event.data?.type === 'world.ready'
        ) {
          finish()
        }
      }
      const onError = (event) => {
        finish(new Error(`Failed to load VM world ${id}`, { cause: event }))
      }

      // A frame's load event does not confirm that its message handler is ready.
      globalThis.addEventListener('message', onReady)
      this.frame.addEventListener('error', onError, { once: true })
      timeout = setTimeout(() => {
        finish(new Error(`VM world ${id} did not initialize`))
      }, 10_000)
    })

    const target =
      globalThis.document.head ??
      globalThis.document.body ??
      globalThis.document

    target.appendChild(this.frame)
  }

  async postMessage (message) {
    await this.ready
    postWindowMessage(this.frame.contentWindow, message, globalThis.location.origin)
  }

  async destroy () {
    // send destroy signal
    await this.postMessage({ id: this.id, type: 'destroy' })
    // wait for state to emit 'destroyed' on instance, after GC
    await new Promise((resolve) => {
      this.addEventListener('destroyed', resolve, { once: true })
    })
  }
}

class State {
  static init () {
    const state = new State()
    state.init()
    return state
  }

  ports = []
  worker = null

  /**
   * A mapping of client IDs to content worlds.
   * @type {Map<String, World>}
   */
  worlds = new Map()
  clients = new Map()

  constructor () {
    this.onMessage = this.onMessage.bind(this)
    this.onWorkerMessage = this.onWorkerMessage.bind(this)
    this.onWorkerMessageError = this.onWorkerMessageError.bind(this)
  }

  init () {
    globalThis.addEventListener('message', this.onMessage)
    hooks.onReady(async () => {
      const currentWindow = await application.getCurrentWindow()

      this.worker = await vm.getContextWorker()
      this.worker.port.addEventListener('message', this.onWorkerMessage)
      this.worker.port.addEventListener(
        'messageerror',
        this.onWorkerMessageError
      )
      this.worker.port.postMessage({ type: 'realm' })

      const announceReady = () => {
        vm.channel.postMessage({ ready: currentWindow.index })
      }

      vm.channel.addEventListener('message', (event) => {
        if (event.data?.probe === currentWindow.index) {
          announceReady()
        }
      })

      announceReady()
    })
  }

  onMessage (event) {
    const data = event.data

    if (data?.type === 'world.result') {
      this.worker.port.postMessage({ ...data, type: 'result' })
    }

    if (data?.type === 'world.destroy') {
      const { id } = data
      const world = this.worlds.get(id)
      if (world) {
        world.dispatchEvent(new Event('destroyed'))
        this.worlds.delete(id)
        if (world.frame?.parentElement) {
          world.frame.parentElement.removeChild(world.frame)
        }
      }
    }
  }

  async onWorkerMessage (event) {
    if (event.data?.type === 'terminate-worker') {
      const pending = []
      for (const world of this.worlds.values()) {
        pending.push(world.destroy())
      }

      await Promise.all(pending)
      const currentWindow = await application.getCurrentWindow()
      await currentWindow.close()
      this.worker = null
    }

    if (event.data?.type === 'script') {
      const { id, nonce } = event.data
      const world = this.worlds.get(id) ?? new World(this, id)

      if (!this.worlds.has(id)) {
        this.worlds.set(id, world)
      }

      try {
        await world.postMessage(serializeWindowMessage(event.data))
      } catch (error) {
        this.worlds.delete(id)
        if (world.frame?.parentElement) {
          world.frame.parentElement.removeChild(world.frame)
        }

        this.worker.port.postMessage({
          type: 'result',
          err: createTransferredError(error),
          nonce,
          id
        })
      }
    }

    if (event.data?.type === 'destroy') {
      const { id } = event.data
      const world = this.worlds.get(id)
      if (world) {
        world.destroy()
      }
    }
  }

  onWorkerMessageError (event) {
    globalThis.reportError(
      event.error ??
        new Error('An unknown VM worker error occurred', { cause: event })
    )
  }
}

function createWorld (options) {
  const frame = globalThis.document.createElement('iframe')

  frame.setAttribute('sandbox', 'allow-same-origin allow-scripts')
  frame.src = `${globalThis.origin}/oro/vm/world.html`
  frame.id = options.id

  Object.assign(frame.style, {
    width: 0,
    height: 0,
    display: 'none'
  })

  return frame
}

if (globalThis.window === globalThis) {
  State.init()
}

export {}
