import { spawnSync } from 'node:child_process'

function run (args, options = {}) {
  const res = spawnSync('oroc', args, {
    encoding: 'utf8',
    shell: process.platform === 'win32',
    ...options
  })
  return res
}

function getEnvOutput () {
  const { status, stdout } = run(['env'])
  if (status !== 0) {
    throw new Error('oroc env exited with status ' + status)
  }
  return stdout || ''
}

const out = getEnvOutput()
const lines = out.split(/\r?\n/)
const kv = new Map()
for (const line of lines) {
  const i = line.indexOf('=')
  if (i > 0) kv.set(line.slice(0, i), line.slice(i + 1))
}

const ip = (kv.get('DETECTED_DEV_HOST') || '').trim()
if (!ip) {
  console.error('DETECTED_DEV_HOST missing from oroc env output')
  process.exit(1)
}

console.log('DETECTED_DEV_HOST:', ip)
console.log('cli ip test passed')
