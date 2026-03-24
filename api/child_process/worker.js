import { parentPort } from '../worker_threads.js'
import process from '../process.js'
import signal from '../process/signal.js'
import ipc from '../ipc.js'

const SPAWN_COMMAND = 'child_process.spawn'
const KILL_COMMAND = 'child_process.kill'

const state = {}

const propagateWorkerError = (err) =>
  parentPort.postMessage({
    worker_threads: {
      error: {
        name: err.name,
        message: err.message,
        stack: err.stack,
        type: err.name
      }
    }
  })

function parseProcessId (value) {
  if (typeof value === 'bigint') return value
  if (typeof value === 'number' && Number.isFinite(value)) {
    return BigInt(value)
  }
  if (typeof value === 'string') {
    const normalized = value.trim()
    if (normalized.length && /^[0-9]+$/.test(normalized)) {
      return BigInt(normalized)
    }
  }
  return null
}

if (process.stdin) {
  process.stdin.on('data', async (data) => {
    const { id } = state
    const result = await ipc.write(SPAWN_COMMAND, { id }, data)

    if (result.err) {
      propagateWorkerError(result.err)
    }
  })
}

parentPort.onmessage = async ({ data: { id, method, args } }) => {
  if (method === 'spawn') {
    const command = args[0]
    const argv = args[1]
    const opts = args[2]

    const params = {
      args: [command, ...Array.from(argv ?? [])].join('\u0001'),
      id,
      cwd: opts?.cwd ?? '',
      stdin: opts?.stdin !== false,
      stdout: opts?.stdout !== false,
      stderr: opts?.stderr !== false
    }

    if (opts?.env && typeof opts.env === 'object' && !Array.isArray(opts.env)) {
      const envEntries = Object.entries(opts.env)
        .filter(([, value]) => value !== undefined)
        .map(([key, value]) => `${key}=${String(value ?? '')}`)

      params.env = envEntries.join('\u0001')
    }

    const result = await ipc.send(SPAWN_COMMAND, params)

    if (result.err) {
      return propagateWorkerError(result.err)
    }

    const spawnId = parseProcessId(result.data?.id)
    if (spawnId == null) {
      return propagateWorkerError(
        new Error('child_process.spawn returned invalid id')
      )
    }

    state.id = spawnId
    state.pid = result.data.pid
    state.spawnfile = command
    state.spawnargs = argv
    state.lifecycle = 'spawn'

    parentPort.postMessage({ method: 'state', args: [state] })

    const matchesState = (value) => {
      if (state.id == null || value == null) return false
      const candidate = parseProcessId(value)
      if (candidate == null) return false
      return candidate === state.id
    }

    globalThis.addEventListener('data', ({ detail }) => {
      const { err, data, source } = detail.params
      const buffer = detail.data

      if (err && matchesState(err.id)) {
        return propagateWorkerError(err)
      }

      if (!data || !matchesState(data.id)) return

      if (source === SPAWN_COMMAND && data.source === 'stdout') {
        if (process.stdout) {
          process.stdout.write(buffer)
        }
      }

      if (source === SPAWN_COMMAND && data.source === 'stderr') {
        if (process.stderr) {
          process.stderr.write(buffer)
        }
      }

      if (source === SPAWN_COMMAND && data.status === 'close') {
        state.exitCode = data.code
        state.lifecycle = 'close'
        parentPort.postMessage({ method: 'state', args: [state] })
      }

      if (source === SPAWN_COMMAND && data.status === 'exit') {
        state.exitCode = data.code
        state.lifecycle = 'exit'
        parentPort.postMessage({ method: 'state', args: [state] })
      }
    })
  }

  if (method === 'kill') {
    const result = await ipc.send(KILL_COMMAND, {
      id: state.id,
      signal: signal.getCode(args[0])
    })

    if (result.err) {
      return propagateWorkerError(result.err)
    }

    state.lifecycle = 'kill'
    parentPort.postMessage({ method: 'state', args: [state] })
  }
}
