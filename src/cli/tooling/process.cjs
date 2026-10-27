'use strict'

const { spawn } = require('node:child_process')
const { AsyncLocalStorage } = require('node:async_hooks')
const path = require('node:path')
const operations = new AsyncLocalStorage()

/**
 * @typedef {object} CommandOptions
 * @property {string} [cwd]
 * @property {object} [env]
 * @property {boolean} [log] Send child stdout to stderr instead of collecting it.
 * @property {number} [timeout] Milliseconds before terminating the command.
 * @property {AbortSignal} [signal]
 */

/**
 * Run nested build commands with one cancellation signal.
 * @param {AbortSignal} signal
 * @param {Function} callback
 * @returns {*}
 */
function operation (signal, callback) { return operations.run(signal, callback) }
/**
 * Return the cancellation signal for the current preparation.
 * @returns {AbortSignal|undefined}
 */
function operationSignal () { return operations.getStore() }

/**
 * Build a literal process invocation, including Windows batch launch rules.
 * @param {string} command
 * @param {string[]} args
 * @returns {object}
 */
function commandLine (command, args) {
  let executable = command
  let argumentsList = args
  let windowsVerbatimArguments = false
  if (process.platform === 'win32' && (command === 'gradle' || /\.(bat|cmd)$/i.test(command))) {
    if ([command, ...args].some(value => /["%!^&|<>\r\n]/.test(value))) {
      throw new Error('Windows batch launch cannot represent these arguments. Use project and tool paths without quotes or cmd.exe expansion characters.')
    }
    executable = process.env.ComSpec || 'cmd.exe'
    argumentsList = ['/d', '/s', '/c', `"${[command, ...args].map(value => `"${value}"`).join(' ')}"`]
    windowsVerbatimArguments = true
  }
  return { command: executable, args: argumentsList, windowsVerbatimArguments }
}

/**
 * Start a process group and return an idempotent, bounded tree shutdown.
 * @param {string} command
 * @param {string[]} args
 * @param {object} options Node spawn options.
 * @returns {object}
 */
function spawnManaged (command, args, options) {
  const launch = commandLine(command, args)
  const child = spawn(launch.command, launch.args, {
    ...options,
    detached: process.platform !== 'win32',
    windowsHide: true,
    windowsVerbatimArguments: launch.windowsVerbatimArguments
  })
  let stopping = false
  let timer
  let closed = false
  let complete
  let forced = false
  const finished = new Promise(resolve => { complete = resolve })
  const kill = signal => {
    if (!child.pid) return
    if (process.platform === 'win32') {
      const killer = spawn(path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'taskkill.exe'), ['/PID', String(child.pid), '/T', '/F'], { stdio: 'ignore', windowsHide: true })
      killer.on('error', () => child.kill())
      killer.on('exit', code => { if (code && !closed) child.kill() })
    } else {
      try { process.kill(-child.pid, signal) } catch (error) { if (error.code !== 'ESRCH') child.emit('error', error) }
    }
  }
  const stop = () => {
    if (stopping) return
    stopping = true
    child.stdin?.end()
    kill('SIGTERM')
    timer = setTimeout(() => { kill('SIGKILL'); forced = true; if (closed) complete() }, 3000)
  }
  child.once('close', () => {
    closed = true
    if (stopping && !forced) return
    clearTimeout(timer)
    complete()
  })
  return { child, stop, finished }
}

/**
 * Run a build tool with cancellation and bounded output.
 * @param {string} command
 * @param {string[]} args
 * @param {CommandOptions} [options]
 * @returns {Promise<string>}
 */
function execute (command, args, options = {}) {
  return new Promise((resolve, reject) => {
    const signal = options.signal || operationSignal()
    if (signal?.aborted) return reject(signal.reason || new Error('Preparation cancelled'))
    const { child, stop, finished } = spawnManaged(command, args, {
      cwd: options.cwd,
      env: options.env,
      stdio: ['ignore', options.log ? 2 : 'pipe', 'pipe']
    })
    let output = ''
    let errors = ''
    let failure
    const cancel = () => { failure ||= signal?.reason || new Error(`${command} was cancelled`); stop() }
    const timer = setTimeout(() => { failure = new Error(`${command} timed out`); cancel() }, options.timeout || 15 * 60 * 1000)
    process.on('SIGINT', cancel)
    process.on('SIGTERM', cancel)
    signal?.addEventListener('abort', cancel, { once: true })
    child.stdout?.on('data', chunk => {
      output += chunk.toString()
      if (output.length > 32 * 1024 * 1024) { failure = new Error(`${command} output exceeds 32 MiB`); cancel() }
    })
    child.stderr.on('data', chunk => {
      if (options.log) process.stderr.write(chunk)
      errors = (errors + chunk.toString()).slice(-65536)
    })
    child.once('error', error => { failure = error; stop() })
    child.once('close', async code => {
      await finished
      clearTimeout(timer)
      process.removeListener('SIGINT', cancel)
      process.removeListener('SIGTERM', cancel)
      signal?.removeEventListener('abort', cancel)
      if (failure || code !== 0) reject(new Error(`${command} failed: ${failure?.message || errors || output || `exit ${code}`}`, { cause: failure }))
      else resolve(output)
    })
  })
}

module.exports = { execute, commandLine, spawnManaged, operation, operationSignal }
