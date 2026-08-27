import { Writable, Readable } from './stream.js'
import { getTransferables } from './vm.js'
import init, { SHARE_ENV } from './worker_threads/init.js'
import { maybeMakeError } from './ipc.js'
import { AsyncResource } from './async/resource.js'
import { EventEmitter } from './events.js'
import { Buffer } from './buffer.js'
import location from './location.js'
import { env } from './process.js'
/**

 * A pool of known worker threads.
 * @type {Map<string, Worker>}
 */
export const workers = new Map()

/**
 * `true` if this is the "main" thread, otherwise `false`
 * The "main" thread is the top level webview window.
 * @type {boolean}
 */
export const isMainThread = init.state.isMainThread

/**
 * The main thread `MessagePort` which is `null` when the
 * current context is not the "main thread".
 * @type {MessagePort?}
 */
export const mainPort = init.state.mainPort

/**
 * A worker thread `BroadcastChannel` class.
 */
export class BroadcastChannel extends globalThis.BroadcastChannel {}

/**
 * A worker thread `MessageChannel` class.
 */
export class MessageChannel extends globalThis.MessageChannel {}

/**
 * A worker thread `MessagePort` class.
 */
export class MessagePort extends globalThis.MessagePort {}

// inherit `EventEmitter`
Object.assign(BroadcastChannel.prototype, EventEmitter.prototype)
Object.assign(MessageChannel.prototype, EventEmitter.prototype)
Object.assign(MessagePort.prototype, EventEmitter.prototype)

/**
 * The current unique thread ID.
 * @type {number}
 */
export const threadId = isMainThread
  ? 0
  : globalThis.RUNTIME_WORKER_ID
    ? parseInt(globalThis.RUNTIME_WORKER_ID ?? '0') || 0
    : parseInt(globalThis.__args.client?.id) || 0

/**
 * The parent `MessagePort` instance
 * @type {MessagePort?}
 */
export const parentPort = init.state.parentPort

/**
 * Transferred "worker data" when creating a new `Worker` instance.
 * @type {any?}
 */
export const workerData = init.state.workerData

/**
 * Set shared worker environment data.
 * @param {string} key
 * @param {any} value
 */
export function setEnvironmentData (key, value) {
  // update this thread state
  init.state.env[key] = value

  for (const worker of workers.values()) {
    const transfer = getTransferables(value)
    worker.postMessage(
      {
        worker_threads: {
          env: { key, value }
        }
      },
      { transfer }
    )
  }
}

/**
 * Get shared worker environment data.
 * @param {string} key
 * @return {any}
 */
export function getEnvironmentData (key) {
  return init.state.env[key] ?? null
}

export class Pipe extends AsyncResource {
  #reading = true

  /**
   * `Pipe` class constructor.
   * @param {Worker} worker
   * @ignore
   */
  constructor (worker) {
    super('Pipe')

    if (worker.stdout) {
      const stdout = worker.stdout
      const { emit } = stdout
      stdout.emit = (...args) => {
        if (!this.reading) return false
        return this.runInAsyncScope(() => {
          return emit.call(stdout, ...args)
        })
      }
    }

    if (worker.stderr) {
      const stderr = worker.stderr
      const { emit } = stderr
      stderr.emit = (...args) => {
        if (!this.reading) return false
        return this.runInAsyncScope(() => {
          return emit.call(stderr, ...args)
        })
      }
    }

    worker.once('close', () => this.destroy())
    worker.once('exit', () => this.destroy())
  }

  /**
   * `true` if the pipe is still reading, otherwise `false`.
   * @type {boolean}
   */
  get reading () {
    return this.#reading
  }

  /**
   * Destroys the pipe
   */
  destroy () {
    this.#reading = false
  }
}

/**
 * @typedef {{
 *   env?: object,
 *   stdin?: boolean = false,
 *   stdout?: boolean = false,
 *   stderr?: boolean = false,
 *   workerData?: any,
 *   transferList?: any[],
 *   eval?: boolean = false
 * }} WorkerOptions

/**
 * A worker thread that can communicate directly with a parent thread,
 * share environment data, and process streamed data.
 */
export class Worker extends EventEmitter {
  /**
   * Emitted when the worker is ready to receive messages.
   * @event Worker#online
   * @type {() => void}
   */
  /**
   * Emitted when the worker posts a message.
   * @event Worker#message
   * @type {(value: any) => void}
   */
  /**
   * Emitted when an error occurs in the worker.
   * @event Worker#error
   * @type {(err: Error) => void}
   */
  /**
   * Emitted when the worker exits.
   * @event Worker#exit
   * @type {(code: number) => void}
   */
  #resource = null
  #worker = null
  #stdin = null
  #stdout = null
  #stderr = null
  #online = false

  /**
   * `Worker` class constructor.
   * @param {string} filename
   * @param {WorkerOptions=} [options]
   */
  constructor (filename, options = null) {
    super()

    options = { ...options }

    const url = new URL(
      '/oro/worker_threads/init.js',
      location.origin
    ).toString()
    this.#resource = new AsyncResource('WorkerThread')

    this.onWorkerMessage = this.onWorkerMessage.bind(this)
    this.onProcessEnvironmentEvent = this.onProcessEnvironmentEvent.bind(this)

    if (options.env === SHARE_ENV) {
      options.env = SHARE_ENV.toString()
      env.addEventListener('set', this.onProcessEnvironmentEvent)
      env.addEventListener('delete', this.onProcessEnvironmentEvent)
    }

    if (options.stdin === true) {
      this.#stdin = new Writable({
        write: (data, cb) => {
          const transfer = getTransferables(data)
          this.#worker.postMessage(
            { worker_threads: { stdin: { data } } },
            { transfer }
          )

          cb(null)
        }
      })
    }

    if (options.stdout === true) {
      this.#stdout = new Readable({
        map: (data) => Buffer.from(data)
      })
    }

    if (options.stderr === true) {
      this.#stderr = new Readable({
        map: (data) => Buffer.from(data)
      })
    }

    this.#worker = new globalThis.Worker(url.toString())
    this.#worker.addEventListener('message', this.onWorkerMessage)

    if (options.workerData) {
      const transfer =
        options.transferList ?? getTransferables(options.workerData)
      const message = {
        worker_threads: { workerData: options.workerData }
      }

      this.#worker.postMessage(message, { transfer })
    }

    this.#worker.postMessage({
      worker_threads: {
        init: {
          id: this.id,
          url: new URL(filename, globalThis.location.href).toString(),
          eval: options?.eval === true,
          process: {
            env: options.env ?? {},
            stdin: options.stdin === true,
            stdout: options.stdout === true,
            stderr: options.stderr === true
          }
        }
      }
    })
  }

  /**
   * The unique ID for this `Worker` thread instance.
   * @type {number}
   */
  get id () {
    return this.#worker.id
  }

  get threadId () {
    return this.id
  }

  /**
   * `true` after the worker has loaded and can receive application messages.
   * @type {boolean}
   */
  get online () {
    return this.#online
  }

  /**
   * A `Writable` standard input stream if `{ stdin: true }` was set when
   * creating this `Worker` instance.
   * @type {import('./stream.js').Writable?}
   */
  get stdin () {
    return this.#stdin
  }

  /**
   * A `Readable` standard output stream if `{ stdout: true }` was set when
   * creating this `Worker` instance.
   * @type {import('./stream.js').Readable?}
   */
  get stdout () {
    return this.#stdout
  }

  /**
   * A `Readable` standard error stream if `{ stderr: true }` was set when
   * creating this `Worker` instance.
   * @type {import('./stream.js').Readable?}
   */
  get stderr () {
    return this.#stderr
  }

  /**
   * Terminates the `Worker` instance
   */
  terminate () {
    this.#worker.terminate()
    workers.delete(this.id)
    this.#worker.removeEventListener('message', this.onWorkerMessage)

    env.removeEventListener('set', this.onProcessEnvironmentEvent)
    env.removeEventListener('delete', this.onProcessEnvironmentEvent)

    if (this.#stdin) {
      this.#stdin.destroy()
      this.#stdin = null
    }

    if (this.#stdout) {
      this.#stdout.destroy()
      this.#stdout = null
    }

    if (this.#stderr) {
      this.#stderr.destroy()
      this.#stderr = null
    }
  }

  /**
   * Handles incoming worker messages.
   * @ignore
   * @param {MessageEvent} event
   */
  onWorkerMessage (event) {
    const request = event.data?.worker_threads ?? {}

    if (request.online?.id) {
      this.#online = true
      workers.set(this.id, this)
      this.#resource.runInAsyncScope(() => {
        this.emit('online')
      })
    }

    if (request.error) {
      this.#resource.runInAsyncScope(() => {
        this.emit('error', maybeMakeError(request.error))
      })
    }

    if (request.process?.stdout?.data && this.#stdout) {
      this.#resource.runInAsyncScope(() => {
        this.#stdout.push(request.process.stdout.data)
      })
    }

    if (request.process?.stderr?.data && this.#stderr) {
      this.#resource.runInAsyncScope(() => {
        this.#stderr.push(request.process.stderr.data)
      })
    }

    if (/set|delete/.test(request.process?.env?.type ?? '')) {
      if (request.process.env.type === 'set') {
        Reflect.set(env, request.process.env.key, request.process.env.value)
      } else if (request.process.env.type === 'delete') {
        Reflect.deleteProperty(env, request.process.env.key)
      }
    }

    if (request.process?.exit) {
      // Ensure full cleanup path (listeners, env hooks, streams, workers map)
      this.terminate()
      this.#resource.runInAsyncScope(() => {
        this.emit('exit', request.process.exit.code ?? 0)
      })
    }

    if (event.data?.worker_threads) {
      event.stopImmediatePropagation()
      return false
    }

    this.#resource.runInAsyncScope(() => {
      this.emit('message', event.data)
    })

    if (mainPort) {
      this.#resource.runInAsyncScope(() => {
        mainPort.dispatchEvent(new MessageEvent('message', event))
      })
    }
  }

  /**
   * Handles process environment change events
   * @ignore
   * @param {import('./process.js').ProcessEnvironmentEvent} event
   */
  onProcessEnvironmentEvent (event) {
    this.#worker.postMessage({
      worker_threads: {
        process: {
          env: {
            type: event.type,
            key: event.key,
            value: event.value
          }
        }
      }
    })
  }

  postMessage (...args) {
    this.#worker.postMessage(...args)
  }
}

export { SHARE_ENV, init }

export default {
  Worker,
  isMainThread,
  parentPort,
  setEnvironmentData,
  getEnvironmentData,
  workerData,
  threadId,
  SHARE_ENV
}
