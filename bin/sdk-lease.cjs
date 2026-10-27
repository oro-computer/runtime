// Loaded only by the managed Linux source SDK launcher. Kernel leases follow
// descendants even if a supervisor dies; released npm packages never load this.
const fs = require('node:fs')
const cp = require('node:child_process')
const { syncBuiltinESMExports } = require('node:module')

const leases = JSON.parse(process.env.ORO_SDK_LEASES || '[]')
for (const lease of leases) {
  const actual = fs.fstatSync(lease.fd)
  const expected = fs.lstatSync(lease.path)
  if (!actual.isFile() || actual.dev !== expected.dev || actual.ino !== expected.ino) {
    throw new Error('Managed SDK lease descriptor differs from its registered file')
  }
}

function optionsWithLeases (options, normalized = false) {
  const result = { ...options }
  const selection = result.stdio ?? 'pipe'
  const stdio = Array.isArray(selection) ? [...selection] : [selection, selection, selection]
  const inherited = leases.map(lease => {
    let fd = 63
    while (stdio[fd] !== undefined && stdio[fd] !== 'ignore') fd++
    while (stdio.length <= fd) stdio.push('ignore')
    stdio[fd] = lease.fd
    return { ...lease, fd }
  })
  result.stdio = stdio
  if (normalized) {
    const pairs = result.envPairs ?? Object.entries(process.env).map(([key, value]) => `${key}=${value}`)
    const oldOptions = pairs.find(pair => pair.startsWith('NODE_OPTIONS='))?.slice(13) ?? ''
    result.envPairs = pairs.filter(pair => !pair.startsWith('ORO_SDK_LEASES=') && !pair.startsWith('NODE_OPTIONS='))
    result.envPairs.push(`ORO_SDK_LEASES=${JSON.stringify(inherited)}`, `NODE_OPTIONS=${preloadOptions(oldOptions)}`)
  } else {
    result.env = { ...(result.env ?? process.env), ORO_SDK_LEASES: JSON.stringify(inherited) }
    result.env.NODE_OPTIONS = preloadOptions(result.env.NODE_OPTIONS ?? '')
  }
  return result
}

function preloadOptions (options) {
  const flag = `--require ${JSON.stringify(__filename)}`
  return options.includes(flag) ? options : `${options} ${flag}`
}

if (leases.length) {
  const spawn = cp.ChildProcess.prototype.spawn
  cp.ChildProcess.prototype.spawn = function (options) {
    return spawn.call(this, optionsWithLeases(options, true))
  }
  const spawnSync = cp.spawnSync
  cp.spawnSync = function (file, args, options) {
    if (!Array.isArray(args)) { options = args; args = [] }
    return spawnSync(file, args, optionsWithLeases(options))
  }
  function checked (result, file, args) {
    if (result.error) throw Object.assign(result.error, result)
    if (result.status !== 0) {
      const message = `Command failed: ${[file, ...args].join(' ')}\n${result.stderr ?? ''}`
      throw Object.assign(new Error(message), result)
    }
    return result.stdout
  }
  cp.execFileSync = function (file, args, options) {
    if (!Array.isArray(args)) { options = args; args = [] }
    const result = cp.spawnSync(file, args, options)
    if (options?.stdio === undefined && result.stderr?.length) process.stderr.write(result.stderr)
    return checked(result, file, args)
  }
  cp.execSync = function (command, options) {
    const result = cp.spawnSync(command, [], { ...options, shell: options?.shell ?? true })
    if (options?.stdio === undefined && result.stderr?.length) process.stderr.write(result.stderr)
    return checked(result, command, [])
  }
  syncBuiltinESMExports()
}
