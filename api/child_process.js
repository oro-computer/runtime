/* global ErrorEvent */
import { AsyncResource } from './async/resource.js'
import { EventEmitter } from './events.js'
import diagnostics from './diagnostics.js'
import { Worker } from './worker_threads.js'
import { Buffer } from './buffer.js'
import { rand64 } from './crypto.js'
import process from './process.js'
import signal from './process/signal.js'
import ipc from './ipc.js'
import gc from './gc.js'
import os from './os.js'

/**
 * @typedef {object} ChildProcessOptions
 * @property {Record<string, string | number | boolean | null | undefined>} [env] Complete child environment. Replaces the inherited environment when provided.
 * @property {string} [cwd] Working directory for the child.
 * @property {boolean} [stdin=true] Open a writable stdin pipe.
 * @property {boolean} [stdout=true] Open a readable stdout pipe.
 * @property {boolean} [stderr=true] Open a readable stderr pipe.
 * @property {AbortSignal} [signal] Signal that terminates the child when aborted.
 * @property {number|string} [killSignal='SIGTERM'] Signal used for timeout or abort termination.
 * @property {number} [timeout] Milliseconds before terminating the child.
 */

/**
 * @typedef {ChildProcessOptions & { encoding?: string, shell?: string }} ExecOptions
 */

/**
 * @typedef {ChildProcessOptions & { encoding?: string }} ExecFileOptions
 */

/**
 * @typedef {Omit<ChildProcessOptions, 'signal'|'stdin'> & { encoding?: string }} ExecSyncOptions
 */

/**
 * @typedef {(error: Error | null, stdout: string | Buffer | null, stderr: string | Buffer | null) => void} ExecCallback
 */

const dc = diagnostics.channels.group('child_process', [
  'spawn',
  'close',
  'exit',
  'kill'
])

function validateEnvironment (env) {
  if (env == null) return
  if (typeof env !== 'object' || Array.isArray(env)) {
    throw new TypeError('Expecting env to be an object.')
  }

  for (const [key, value] of Object.entries(env)) {
    if (!key || key.includes('=') || key.includes('\u0000') || key.includes('\u0001')) {
      throw new TypeError('Environment names cannot be empty or contain =, NUL, or U+0001 characters.')
    }
    const stringValue = String(value ?? '')
    if (stringValue.includes('\u0000') || stringValue.includes('\u0001')) {
      throw new TypeError('Environment values cannot contain NUL or U+0001 characters.')
    }
  }
}

function serializeEnvironment (env) {
  validateEnvironment(env)
  return Object.entries(env)
    .filter(([, value]) => value !== undefined)
    .map(([key, value]) => `${key}=${String(value ?? '')}`)
    .join('\u0001')
}

function normalizeSpawnInput (command, args, options) {
  if (args && typeof args === 'object' && !Array.isArray(args)) {
    options = args
    args = []
  }

  if (!command || typeof command !== 'string') {
    throw new TypeError('Expecting command to be a string.')
  }

  if (command.includes('\u0000') || command.includes('\u0001')) {
    throw new TypeError('Command cannot contain NUL or U+0001 characters.')
  }

  if (!Array.isArray(args) || args.some((arg) => typeof arg !== 'string')) {
    throw new TypeError('Expecting args to be an array of strings.')
  }

  if (args.some((arg) => arg.includes('\u0000') || arg.includes('\u0001'))) {
    throw new TypeError('Arguments cannot contain NUL or U+0001 characters.')
  }

  if (options != null && (typeof options !== 'object' || Array.isArray(options))) {
    throw new TypeError('Expecting options to be an object.')
  }

  if (options?.cwd != null && typeof options.cwd !== 'string') {
    throw new TypeError('Expecting cwd to be a string.')
  }
  if (options?.cwd?.includes('\u0000')) {
    throw new TypeError('Working directory cannot contain NUL characters.')
  }
  if (options?.timeout != null &&
      (!Number.isFinite(options.timeout) || options.timeout < 0)) {
    throw new TypeError('Expecting timeout to be a non-negative finite number.')
  }
  for (const key of ['stdin', 'stdout', 'stderr']) {
    if (options?.[key] != null && typeof options[key] !== 'boolean') {
      throw new TypeError(`Expecting ${key} to be a boolean.`)
    }
  }
  validateEnvironment(options?.env)

  return { args, options }
}

export class Pipe extends AsyncResource {
  #process = null
  #reading = true

  /**
   * `Pipe` class constructor.
   * @param {ChildProcess} process
   * @ignore
   */
  constructor (process) {
    super('Pipe')

    this.#process = process

    if (process.stdout) {
      const stdout = process.stdout
      const { emit } = stdout
      stdout.emit = (...args) => {
        if (!this.reading) return false
        return this.runInAsyncScope(() => {
          return emit.call(stdout, ...args)
        })
      }
    }

    if (process.stderr) {
      const stderr = process.stderr
      const { emit } = stderr
      stderr.emit = (...args) => {
        if (!this.reading) return false
        return this.runInAsyncScope(() => {
          return emit.call(stderr, ...args)
        })
      }
    }

    // `exit` can be emitted before the child's stdio has been delivered. Keep
    // the pipe readable until `close`, which represents the end of stdio.
    process.once('close', () => this.destroy())
  }

  /**
   * `true` if the pipe is still reading, otherwise `false`.
   * @type {boolean}
   */
  get reading () {
    return this.#reading
  }

  /**
   * @type {import('./process')}
   */
  get process () {
    return this.#process
  }

  /**
   * Destroys the pipe
   */
  destroy () {
    this.#reading = false
  }
}

export class ChildProcess extends EventEmitter {
  /**
   * Emitted when the process has spawned successfully.
   * @event ChildProcess#spawn
   * @type {() => void}
   */
  /**
   * Emitted when the process exits.
   * @event ChildProcess#exit
   * @type {(code: number|null) => void}
   */
  /**
   * Emitted when the stdio streams of a child process have been closed.
   * @event ChildProcess#close
   * @type {(code: number|null) => void}
   */
  /**
   * Emitted when an error occurs on the child process.
   * @event ChildProcess#error
   * @type {(err: Error) => void}
   */
  #id = rand64()
  #worker = null
  #signal = null
  #onAbort = null
  #timeout = null
  #resource = null
  #env = { ...process.env }
  #pipe = null
  #onWorkerMessage = null
  #onWorkerError = null

  #state = {
    killed: false,
    signalCode: null,
    exitCode: null,
    spawnfile: null,
    spawnargs: [],
    lifecycle: 'init',
    pid: 0
  }

  /**
   * `ChildProcess` class constructor.
   * @param {ChildProcessOptions} [options]
   */
  constructor (options = null) {
    super()

    // this does not implement disconnect or message because this is not node
    // @ts-ignore
    const workerLocation = new URL('./child_process/worker.js', import.meta.url)

    // TODO(@jwerle): support environment variable inject
    if (options?.env && typeof options?.env === 'object') {
      this.#env = options.env
    }

    this.#resource = new AsyncResource('ChildProcess')
    this.#resource.handle = this

    this.#resource.runInAsyncScope(() => {
      this.#worker = new Worker(workerLocation.toString(), {
        env: options?.env ?? {},
        stdin: options?.stdin !== false,
        stdout: options?.stdout !== false,
        stderr: options?.stderr !== false,
        workerData: { id: this.#id }
      })

      this.#pipe = new Pipe(this)
    })

    if (options?.signal) {
      this.#signal = options.signal
      this.#onAbort = () => {
        this.#resource.runInAsyncScope(() => {
          this.emit('error', new Error(this.#signal.reason))
          this.kill(options?.killSignal ?? 'SIGKILL')
        })
      }
      this.#signal.addEventListener('abort', this.#onAbort, { once: true })
      const removeAbortListener = () => {
        try {
          this.#signal?.removeEventListener('abort', this.#onAbort)
        } catch {}
        this.#onAbort = null
        this.#signal = null
      }
      this.once('close', removeAbortListener)
      this.once('exit', removeAbortListener)
    }

    if (options?.timeout) {
      this.#timeout = setTimeout(() => {
        this.#resource.runInAsyncScope(() => {
          this.emit('error', new Error('Child process timed out'))
          this.kill(options?.killSignal ?? 'SIGKILL')
        })
      }, options.timeout)

      this.once('exit', () => {
        clearTimeout(this.#timeout)
      })
    }

    this.#onWorkerMessage = (data) => {
      if (data.method === 'kill' && data.args[0] === true) {
        this.#state.killed = true
      }

      if (data.method === 'state') {
        if (this.#state.pid !== data.args[0].pid) {
          this.#resource.runInAsyncScope(() => {
            this.emit('spawn')
          })
        }

        Object.assign(this.#state, data.args[0])

        switch (this.#state.lifecycle) {
          case 'spawn': {
            gc.ref(this)
            dc.channel('spawn').publish({ child_process: this })
            break
          }

          case 'exit': {
            this.#resource.runInAsyncScope(() => {
              this.emit('exit', this.#state.exitCode)
            })
            dc.channel('exit').publish({ child_process: this })
            break
          }

          case 'close': {
            const exitCode = this.#state.exitCode
            // Readable streams schedule delivery from push() in a microtask.
            // Defer close so all stdout/stderr queued before the native close
            // notification is observable first.
            queueMicrotask(() => {
              this.#resource.runInAsyncScope(() => {
                this.emit('close', exitCode)
              })

              dc.channel('close').publish({ child_process: this })
            })
            break
          }

          case 'kill': {
            this.#state.killed = true
            dc.channel('kill').publish({ child_process: this })
            break
          }
        }
      }

      if (data.method === 'exit') {
        this.#resource.runInAsyncScope(() => {
          this.emit('exit', data.args[0])
        })
      }
    }
    this.#worker.on('message', this.#onWorkerMessage)

    this.#onWorkerError = (err) => {
      this.#resource.runInAsyncScope(() => {
        this.emit('error', err)
      })
    }
    this.#worker.on('error', this.#onWorkerError)

    const cleanupWorker = () => {
      try {
        this.#worker?.off?.('message', this.#onWorkerMessage)
      } catch {}
      try {
        this.#worker?.off?.('error', this.#onWorkerError)
      } catch {}
      this.#onWorkerMessage = null
      this.#onWorkerError = null
      this.#worker?.terminate()
      gc.unref(this)
    }
    this.once('close', cleanupWorker)
    this.once('error', cleanupWorker)
  }

  /**
   * @ignore
   * @type {Pipe}
   */
  get pipe () {
    return this.#pipe
  }

  /**
   * `true` if the child process was killed with kill()`,
   * otherwise `false`.
   * @type {boolean}
   */
  get killed () {
    return this.#state.killed
  }

  /**
   * The process identifier for the child process. This value is
   * `> 0` if the process was spawned successfully, otherwise `0`.
   * @type {number}
   */
  get pid () {
    return this.#state.pid
  }

  /**
   * The executable file name of the child process that is launched. This
   * value is `null` until the child process has successfully been spawned.
   * @type {string?}
   */
  get spawnfile () {
    return this.#state.spawnfile ?? null
  }

  /**
   * The full list of command-line arguments the child process was spawned with.
   * This value is an empty array until the child process has successfully been
   * spawned.
   * @type {string[]}
   */
  get spawnargs () {
    return this.#state.spawnargs
  }

  /**
   * Always `false` as the IPC messaging is not supported.
   * @type {boolean}
   */
  get connected () {
    return false
  }

  /**
   * The child process exit code. This value is `null` if the child process
   * is still running, otherwise it is a positive integer.
   * @type {number?}
   */
  get exitCode () {
    return this.#state.exitCode ?? null
  }

  /**
   * If available, the underlying `stdin` writable stream for
   * the child process.
   * @type {import('./stream').Writable?}
   */
  get stdin () {
    return this.#worker.stdin ?? null
  }

  /**
   * If available, the underlying `stdout` readable stream for
   * the child process.
   * @type {import('./stream').Readable?}
   */
  get stdout () {
    return this.#worker.stdout ?? null
  }

  /**
   * If available, the underlying `stderr` readable stream for
   * the child process.
   * @type {import('./stream').Readable?}
   */
  get stderr () {
    return this.#worker.stderr ?? null
  }

  /**
   * The underlying worker thread.
   * @ignore
   * @type {import('./worker_threads').Worker}
   */
  get worker () {
    return this.#worker
  }

  /**
   * This function does nothing, but is present for nodejs compat.
   */
  disconnect () {
    return false
  }

  /**
   * This function does nothing, but is present for nodejs compat.
   * @return {boolean}
   */
  send () {
    return false
  }

  /**
   * This function does nothing, but is present for nodejs compat.
   */
  ref () {
    return false
  }

  /**
   * This function does nothing, but is present for nodejs compat.
   */
  unref () {
    return false
  }

  /**
   * Kills the child process. This function throws an error if the child
   * process has not been spawned or is already killed.
   * @param {number|string} signal
   */
  kill (...args) {
    if (!/spawn/.test(this.#state.lifecycle)) {
      throw new Error('Cannot kill a child process that has not been spawned')
    }

    if (this.killed) {
      throw new Error('Cannot kill an already killed child process')
    }

    const signalCode = args.length > 0 ? args[0] : 'SIGTERM'
    this.#worker.postMessage({
      id: this.#id,
      method: 'kill',
      args: [signalCode]
    })
    return this
  }

  /**
   * Spawns the child process. This function will throw an error if the process
   * is already spawned.
   * @param {string} command Executable name or path.
   * @param {string[]|ChildProcessOptions} [args] Tokenized arguments or options.
   * @param {ChildProcessOptions} [options] Spawn options.
   * @return {ChildProcess}
   */
  spawn (command, args = [], options = null) {
    if (/spawning|spawn/.test(this.#state.lifecycle)) {
      throw new Error('Cannot spawn an already spawned ChildProcess')
    }

    const normalized = normalizeSpawnInput(command, args, options)

    this.#state.lifecycle = 'spawning'
    this.#worker.postMessage({
      id: this.#id,
      env: this.#env,
      method: 'spawn',
      args: [command, normalized.args, normalized.options]
    })

    return this
  }

  /**
   * `EventTarget` based `addEventListener` method.
   * @param {string} event
   * @param {function(Event)} callback
   * @param {{ once?: false }} [options]
   */
  addEventListener (event, callback, options = null) {
    callback.listener = (...args) => {
      if (event === 'error') {
        callback(
          new ErrorEvent('error', {
            // @ts-ignore
            target: this,
            error: args[0]
          })
        )
      } else {
        callback(new Event(event, args[0]))
      }
    }

    if (options?.once === true) {
      this.once(event, callback.listener)
    } else {
      this.on(event, callback.listener)
    }
  }

  /**
   * `EventTarget` based `removeEventListener` method.
   * @param {string} event
   * @param {function(Event)} callback
   * @param {{ once?: false }} [options]
   */
  removeEventListener (event, callback) {
    this.off(event, callback.listener ?? callback)
  }

  /**
   * Implements `gc.finalizer` for gc'd resource cleanup.
   * @return {import('./gc.js').Finalizer}
   * @ignore
   */
  [gc.finalizer] () {
    return {
      args: [this.#id],
      async handle (id) {
        const result = await ipc.send('child_process.kill', {
          id,
          signal: 'SIGTERM'
        })

        if (result.err) {
          console.warn(result.err)
        }
      }
    }
  }
}

/**
 * Spawns a child process executing `command` directly with `args`.
 * @param {string} command
 * @param {string[]|ChildProcessOptions} [args]
 * @param {ChildProcessOptions} [options]
 * @return {ChildProcess}
 */
export function spawn (command, args = [], options = null) {
  const normalized = normalizeSpawnInput(command, args, options)

  const child = new ChildProcess(normalized.options)
  let started = false
  const start = () => {
    if (started) return
    started = true
    child.worker.off('online', start)
    child.spawn(command, normalized.args, normalized.options)
  }
  child.worker.on('online', start)
  if (child.worker.online) queueMicrotask(start)
  return child
}

function captureExecOutput (child, options, callback) {
  const stdout = []
  const stderr = []
  let closed = false
  let hasError = false

  if (child.stdout) {
    child.stdout.on('data', (data) => {
      if (hasError || closed) {
        return
      }

      stdout.push(Buffer.from(data))
    })
  }

  if (child.stderr) {
    child.stderr.on('data', (data) => {
      if (hasError || closed) {
        return
      }

      stderr.push(Buffer.from(data))
    })
  }

  // Register completion listeners immediately. Fast children can close before
  // Promise assimilation calls this object's then() method.
  const completion = new Promise((resolve, reject) => {
    child.once('error', (err) => {
      hasError = true
      stdout.splice(0, stdout.length)
      stderr.splice(0, stderr.length)
      reject(err)
      if (typeof callback === 'function') {
        callback(err, null, null)
      }
    })

    child.once('close', () => {
      closed = true

      if (hasError) {
        return
      }

      let result
      if (options?.encoding === 'buffer') {
        result = {
          stdout: Buffer.concat(stdout),
          stderr: Buffer.concat(stderr)
        }
      } else {
        const encoding = options?.encoding ?? 'utf8'
        result = {
          // @ts-ignore
          stdout: Buffer.concat(stdout).toString(encoding),
          // @ts-ignore
          stderr: Buffer.concat(stderr).toString(encoding)
        }
      }

      stdout.splice(0, stdout.length)
      stderr.splice(0, stderr.length)
      resolve(result)
      if (typeof callback === 'function') {
        callback(null, result.stdout, result.stderr)
      }
    })
  })

  // Callback-only callers do not consume the thenable. Mark the shared
  // completion rejection handled while preserving it for later consumers.
  completion.catch(() => {})

  // Intentionally make the ChildProcess awaitable.
  return Object.assign(child, {
    // oxlint-disable-next-line unicorn/no-thenable
    then (resolve, reject) {
      return completion.then(resolve, reject)
    },

    catch (reject) {
      return completion.catch(reject)
    },

    finally (next) {
      return completion.finally(next)
    }
  })
}

/**
 * Executes a command string through the platform shell.
 * @param {string} command
 * @param {ExecOptions|ExecCallback} [options]
 * @param {ExecCallback} [callback]
 * @return {ChildProcess & PromiseLike<{ stdout: string | Buffer, stderr: string | Buffer }>}
 */
export function exec (command, options = null, callback = null) {
  if (typeof options === 'function') {
    callback = options
    options = {}
  }

  if (!command || typeof command !== 'string') {
    throw new TypeError('Expecting command to be a string.')
  }

  const shell = options?.shell || (/win32/i.test(os.platform())
    ? process.env.ComSpec || process.env.COMSPEC || 'cmd.exe'
    : '/bin/sh')
  const shellArgs = /win32/i.test(os.platform())
    ? ['/d', '/s', '/c', command]
    : ['-c', command]
  const child = spawn(shell, shellArgs, options)
  return captureExecOutput(child, options, callback)
}

/**
 * Executes a file directly with tokenized arguments.
 * @param {string} file
 * @param {string[]|ExecFileOptions|ExecCallback} [args]
 * @param {ExecFileOptions|ExecCallback} [options]
 * @param {ExecCallback} [callback]
 * @return {ChildProcess & PromiseLike<{ stdout: string | Buffer, stderr: string | Buffer }>}
 */
export function execFile (file, args = [], options = null, callback = null) {
  if (typeof args === 'function') {
    callback = args
    args = []
    options = {}
  } else if (args && typeof args === 'object' && !Array.isArray(args)) {
    callback = typeof options === 'function' ? options : callback
    options = args
    args = []
  } else if (typeof options === 'function') {
    callback = options
    options = {}
  }

  const child = spawn(file, args, options)
  return captureExecOutput(child, options, callback)
}

/**
 * Executes a command string synchronously through the platform shell.
 * @param {string} command
 * @param {ExecSyncOptions} [options]
 * @return {string|Buffer}
 */
export function execSync (command, options) {
  normalizeSpawnInput(command, [], options)

  const decodeOutput = (value) => {
    if (typeof value !== 'string') {
      return value
    }
    try {
      return decodeURIComponent(value)
    } catch {
      return value
    }
  }

  const params = {
    id: rand64(),
    args: command,
    cwd: options?.cwd ?? '',
    stdin: options?.stdin !== false,
    stdout: options?.stdout !== false,
    stderr: options?.stderr !== false,
    timeout: Number.isFinite(options?.timeout) ? options.timeout : 0,
    killSignal: options?.killSignal ?? signal.SIGTERM
  }
  if (options?.env !== undefined) {
    params.env = serializeEnvironment(options.env)
  }

  const result = ipc.sendSync('child_process.exec', params)

  if (result.err) {
    // @ts-ignore
    if (!result.err.code) {
      throw result.err
    }

    // @ts-ignore
    let { stdout, stderr, signal: errorSignal, code, pid } = result.err

    stdout = decodeOutput(stdout)
    stderr = decodeOutput(stderr)

    const message = code === 'ETIMEDOUT' ? 'execSync ETIMEDOUT' : stderr

    const error = Object.assign(new Error(message), {
      pid,
      stdout,
      stderr,
      code: typeof code === 'string' ? code : null,
      signal: errorSignal || signal.toString(options?.killSignal) || null,
      status: Number.isFinite(code) ? code : null,
      output: [null, stdout, stderr]
    })

    // @ts-ignore
    error.error = error

    if (typeof code === 'string') {
      // @ts-ignore
      error.errno = -os.constants.errno[code]
    }

    throw error
  }

  let { stdout, stderr, signal: errorSignal, code, pid } = result.data

  stdout = decodeOutput(stdout)
  stderr = decodeOutput(stderr)

  if (code) {
    const message = code === 'ETIMEDOUT' ? 'execSync ETIMEDOUT' : stderr

    const error = Object.assign(new Error(message), {
      pid,
      stdout,
      stderr,
      code: typeof code === 'string' ? code : null,
      signal: errorSignal || null,
      status: Number.isFinite(code) ? code : null,
      output: [null, stdout, stderr]
    })

    // @ts-ignore
    error.error = error

    if (typeof code === 'string') {
      // @ts-ignore
      error.errno = -os.constants.errno[code]
    }

    throw error
  }

  const output =
    stdout && options?.encoding === 'utf8' ? stdout : Buffer.from(stdout)

  return output
}

exec[Symbol.for('nodejs.util.promisify.custom')] = exec[
  Symbol.for('oro.runtime.util.promisify.custom')
] = async function execPromisify (command, options) {
  return await new Promise((resolve, reject) => {
    exec(command, options, (err, stdout, stderr) => {
      if (err) {
        reject(err)
      } else {
        resolve({ stdout, stderr })
      }
    })
  })
}

export default {
  ChildProcess,
  spawn,
  execFile,
  exec,
  execSync
}
