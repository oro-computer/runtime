/**
 * @module process
 *
 * Example usage:
 * ```js
 * import process from 'oro:process'
 * ```
 */
import { primordials, send } from './ipc.js'
import { EventEmitter } from './events.js'
import signal from './process/signal.js'
import tty from './tty.js'
import os from './os.js'

let didEmitExitEvent = false
let cwd = primordials.cwd

/**
 * @typedef {Object} ProcessVersionsMap
 * @property {string} oro - Current Oro Runtime semantic version.
 * @property {string} [uv]
 * @property {string} [llama]
 * @property {string} [whisper]
 * @property {string} [iroh]
 * @property {string} [sqlite]
 * @property {string} [libusb]
 * @property {string} [libsodium]
 * @property {string} [mbedtls]
 * @property {string} [cpp_httplib]
 * @property {string} [nlohmann_json]
 */

/**
 * Value stored on the observable process environment proxy.
 * @typedef {unknown} ProcessEnvironmentValue
 */

/**
 * Shape of environment change notifications dispatched by `env`.
 * @typedef {{ key: string|symbol, value: ProcessEnvironmentValue }} ProcessEnvironmentMutation
 */

/**
 * Proxy object returned by `process.env`.
 * @typedef {Record<string, ProcessEnvironmentValue>} ProcessEnvironmentProxy
 */

/**
 * Exported `env` binding, combining the event target with the proxy exposed as
 * `process.env`.
 * @typedef {ProcessEnvironment & { readonly proxy: ProcessEnvironmentProxy }} ProcessEnvironmentBinding
 */

/**
 * Event emitted by `env` when an environment variable is set, deleted, or
 * otherwise changed.
 */
export class ProcessEnvironmentEvent extends Event {
  /** @type {string|symbol} */
  key
  /** @type {ProcessEnvironmentValue} */
  value

  /**
   * @param {string} type
   * @param {string|symbol} key
   * @param {ProcessEnvironmentValue=} [value]
   */
  constructor (type, key, value) {
    super(type)
    this.key = key
    this.value = value ?? process.env[key] ?? undefined
  }
}

/**
 * Observable environment store backing `process.env`.
 *
 * Listen on this object to receive `set`, `delete`, and `change` events while
 * reading environment values through `process.env` or `env.proxy`.
 */
export class ProcessEnvironment extends EventTarget {
  get [Symbol.toStringTag] () {
    return 'ProcessEnvironment'
  }
}

/**
 * Emitted when an environment variable is set.
 * @event ProcessEnvironment#set
 * @type {ProcessEnvironmentMutation}
 */
/**
 * Emitted when an environment variable is deleted.
 * @event ProcessEnvironment#delete
 * @type {ProcessEnvironmentMutation}
 */
/**
 * Emitted when an environment variable is changed (set or delete).
 * @event ProcessEnvironment#change
 * @type {ProcessEnvironmentMutation}
 */

/**
 * Observable process-environment state.
 *
 * `process.env` returns `env.proxy`, which behaves like a mutable object of
 * string keys to string values. The exported `env` object itself is the
 * `EventTarget` you can subscribe to for environment change events.
 */
export const env = /** @type {ProcessEnvironmentBinding} */ (
  Object.defineProperties(new ProcessEnvironment(), {
    proxy: {
      configurable: false,
      enumerable: false,
      writable: false,
      value: new Proxy(
        {},
        {
          get (_, property) {
            if (Reflect.has(env, property)) {
              return Reflect.get(env, property)
            }

            return Reflect.get(globalThis.__args.env, property)
          },

          set (_, property, value) {
            if (Reflect.get(env, property) !== value) {
              env.dispatchEvent(
                new ProcessEnvironmentEvent('set', property, value)
              )
              env.dispatchEvent(
                new ProcessEnvironmentEvent('change', property, value)
              )
            }
            return Reflect.set(env, property, value)
          },

          deleteProperty (_, property) {
            if (Reflect.has(env, property)) {
              // @ts-ignore
              env.dispatchEvent(new ProcessEnvironmentEvent('delete', property))
              env.dispatchEvent(new ProcessEnvironmentEvent('change', property))
            }
            return Reflect.deleteProperty(env, property)
          },

          getOwnPropertyDescriptor (_, property) {
            if (Reflect.has(globalThis.__args.env, property)) {
              return {
                configurable: true,
                enumerable: true,
                writable: true,
                value: globalThis.__args.env[property]
              }
            }
          },

          has (_, property) {
            return (
              Reflect.has(env, property) ||
              Reflect.has(globalThis.__args.env, property)
            )
          },

          ownKeys (_) {
            const keys = []
            keys.push(...Reflect.ownKeys(env))
            keys.push(...Reflect.ownKeys(globalThis.__args.env))
            return Array.from(new Set(keys))
          }
        }
      )
    }
  })
)

class Process extends EventEmitter {
  /**
   * Emitted when the process is about to exit. (exit is emitted afterward.)
   * @event Process#exit
   * @type {(code: number) => void}
   */
  /**
   * Named OS signal events may be emitted, e.g., 'SIGINT', 'SIGTERM'.
   * Handlers receive the signal name, numeric code, and message.
   * @event Process#SIGINT
   * @type {(name: string, code: number, message: string) => void}
   */
  // @ts-ignore
  stdin = new tty.ReadStream(0)
  // @ts-ignore
  stdout = new tty.WriteStream(1)
  // @ts-ignore
  stderr = new tty.WriteStream(2)

  get version () {
    return primordials.version.short
  }

  get platform () {
    return primordials.platform
  }

  get env () {
    return env.proxy
  }

  get arch () {
    return primordials.arch
  }

  get argv () {
    return globalThis.__args?.argv ?? []
  }

  get argv0 () {
    return this.argv[0] ?? ''
  }

  get execArgv () {
    return []
  }

  /**
   * Reports current Oro Runtime and native library versions.
   * @returns {ProcessVersionsMap}
   */
  get versions () {
    const versions = { oro: this.version }

    const libraries = {
      uv: primordials.uv?.version,
      llama: primordials.llama?.version,
      whisper: primordials.whisper?.version,
      iroh: primordials.iroh?.version,
      sqlite: primordials.sqlite?.version,
      libusb: primordials.libusb?.version,
      libsodium: primordials.libsodium?.version,
      mbedtls: primordials.mbedtls?.version,
      cpp_httplib: primordials.cpp_httplib?.version,
      nlohmann_json: primordials.nlohmann_json?.version
    }

    for (const [name, value] of Object.entries(libraries)) {
      if (typeof value === 'string' && value.length > 0) {
        versions[name] = value
      }
    }

    return versions
  }

  uptime () {
    return os.uptime()
  }

  cwd () {
    return cwd
  }

  exit (code) {
    return exit(code)
  }

  nextTick (callback, ...args) {
    return nextTick(callback, ...args)
  }

  hrtime (time = [0, 0]) {
    return hrtime(time)
  }

  memoryUsage () {
    return memoryUsage
  }

  chdir (dir) {
    cwd = dir
  }
}

const isNode = Boolean(globalThis.process?.versions?.node)
const process = isNode ? globalThis.process : new Process()

if (!isNode) {
  EventEmitter.call(process)
}

if (!isNode) {
  signal.channel.addEventListener('message', (event) => {
    if (event.data.signal) {
      const code = event.data.signal
      const name = signal.getName(code)
      const message = signal.getMessage(code)
      process.emit(name, name, code, message)
    }
  })

  globalThis.addEventListener('signal', (event) => {
    // @ts-ignore
    if (event.detail.signal) {
      // @ts-ignore
      const code = event.detail.signal
      const name = signal.getName(code)
      const message = signal.getMessage(code)
      process.emit(name, name, code, message)
    }
  })
}

export default process

/**
 * Adds callback to the 'nextTick' queue.
 * @param {Function} callback
 */
export function nextTick (callback, ...args) {
  if (
    isNode &&
    typeof process.nextTick === 'function' &&
    process.nextTick !== nextTick
  ) {
    process.nextTick(callback, ...args)
  } else if (typeof globalThis.queueMicrotask === 'function') {
    globalThis.queueMicrotask(() => {
      try {
        // eslint-disable-next-line
        callback(...args)
      } catch (err) {
        setTimeout(() => {
          throw err
        })
      }
    })
  } else if (typeof globalThis.setImmediate === 'function') {
    globalThis.setImmediate(callback, ...args)
  } else if (typeof globalThis.setTimeout === 'function') {
    globalThis.setTimeout(callback, ...args)
  } else if (typeof globalThis.requestAnimationFrame === 'function') {
    // eslint-disable-next-line
    globalThis.requestAnimationFrame(() => callback(...args))
  } else {
    throw new TypeError("'process.nextTick' is not supported in environment.")
  }
}

if (typeof process.nextTick !== 'function') {
  process.nextTick = nextTick
}

/**
 * Computed high resolution time as a `BigInt`.
 * @param {Array<number>?} [time]
 * @return {bigint}
 */
export function hrtime (time = [0, 0]) {
  if (!time) time = [0, 0]
  if (time && (!Array.isArray(time) || time.length !== 2)) {
    throw new TypeError('Expecting time to be an array of 2 numbers.')
  }

  const value = os.hrtime()
  const seconds = BigInt(1e9)
  const x = value / seconds
  const y = value - x * seconds
  return [Number(x) - time[0], Number(y) - time[1]]
}

hrtime.bigint = function bigint () {
  return os.hrtime()
}

if (typeof process.hrtime !== 'function') {
  process.hrtime = hrtime
}

process.hrtime.bigint = hrtime.bigint

/**
 * @param {number=} [code=0] - The exit code. Default: 0.
 */
export async function exit (code) {
  if (!didEmitExitEvent) {
    didEmitExitEvent = true
    queueMicrotask(() => process.emit('exit', code))
    await send('application.exit', { value: code ?? 0 })
  }
}

/**
 * Returns an object describing the memory usage of the Node.js process measured in bytes.
 * @returns {Object}
 */
export function memoryUsage () {
  const rss = memoryUsage.rss()
  return {
    rss
  }
}

if (typeof process.memoryUsage !== 'function') {
  process.memoryUsage = memoryUsage
}

memoryUsage.rss = function rss () {
  const rusage = os.rusage()
  return rusage.ru_maxrss
}

process.memoryUsage.rss = memoryUsage.rss
