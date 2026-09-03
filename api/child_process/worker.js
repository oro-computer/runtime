import { parentPort } from '../worker_threads.js'
import process from '../process.js'
import signal from '../process/signal.js'
import ipc from '../ipc.js'
import { Writable } from '../stream.js'

const SPAWN_COMMAND = 'child_process.spawn'
const KILL_COMMAND = 'child_process.kill'

const state = {}
let outputCount = 0
let exited = false
let pendingClose = null
let flushingClose = false

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
    const requestId = parseProcessId(id)
    if (requestId == null) {
      return propagateWorkerError(
        new Error('child_process.spawn received an invalid id')
      )
    }
    state.id = requestId

    const matchesState = (value) => {
      if (state.id == null || value == null) return false
      const candidate = parseProcessId(value)
      if (candidate == null) return false
      return candidate === state.id
    }

    let ready = false
    const pendingEvents = []
    const flushClose = async () => {
      if (!pendingClose || !exited || flushingClose) return
      const expectedOutputCount = Number(pendingClose.outputCount ?? 0)
      if (outputCount < expectedOutputCount) return

      flushingClose = true
      await Promise.all([
        process.stdout ? Writable.drained(process.stdout) : true,
        process.stderr ? Writable.drained(process.stderr) : true
      ])

      if (!pendingClose) {
        flushingClose = false
        return
      }

      state.exitCode = pendingClose.code
      state.lifecycle = 'close'
      parentPort.postMessage({ method: 'state', args: [state] })
      pendingClose = null
      globalThis.removeEventListener('data', onData)
    }
    const handleProcessEvent = (detail) => {
      const { err, data, source } = detail.params
      const buffer = detail.data

      if (err && matchesState(err.id)) {
        globalThis.removeEventListener('data', onData)
        propagateWorkerError(err)
        return
      }

      if (!data || !matchesState(data.id)) return

      if (source === SPAWN_COMMAND && data.source === 'stdout') {
        if (process.stdout) {
          process.stdout.write(buffer)
        }
        outputCount++
        flushClose()
      }

      if (source === SPAWN_COMMAND && data.source === 'stderr') {
        if (process.stderr) {
          process.stderr.write(buffer)
        }
        outputCount++
        flushClose()
      }

      if (source === SPAWN_COMMAND && data.status === 'close') {
        pendingClose = data
        flushClose()
      }

      if (source === SPAWN_COMMAND && data.status === 'exit') {
        state.exitCode = data.code
        state.lifecycle = 'exit'
        parentPort.postMessage({ method: 'state', args: [state] })
        exited = true
        flushClose()
      }
    }

    const onData = ({ detail }) => {
      if (!ready) {
        pendingEvents.push(detail)
        return
      }
      handleProcessEvent(detail)
    }
    globalThis.addEventListener('data', onData)

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
      globalThis.removeEventListener('data', onData)
      return propagateWorkerError(result.err)
    }

    const spawnId = parseProcessId(result.data?.id)
    const childPid = parseProcessId(result.data?.pid)
    if (
      spawnId == null ||
      spawnId !== requestId ||
      childPid == null ||
      childPid <= 0n
    ) {
      globalThis.removeEventListener('data', onData)
      return propagateWorkerError(
        new Error(
          'child_process.spawn returned invalid process identifiers ' +
          `(request id ${String(requestId)}, response id ${String(result.data?.id)}, ` +
          `pid ${String(result.data?.pid)})`
        )
      )
    }

    state.pid = Number(childPid)
    state.spawnfile = command
    state.spawnargs = argv
    state.lifecycle = 'spawn'

    parentPort.postMessage({ method: 'state', args: [state] })
    ready = true
    for (const detail of pendingEvents) {
      handleProcessEvent(detail)
    }
    pendingEvents.splice(0, pendingEvents.length)
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
