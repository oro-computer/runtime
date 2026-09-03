import { rmSync as rm, cpSync as cp } from 'node:fs'
import { execSync, spawn } from 'node:child_process'
import path from 'node:path'
import { resolveOrocExecutable } from './oroc-path.js'

const dirname = path.dirname(
  import.meta.url.replace('file://', '').replace(/^\/[A-Za-z]:/, '')
)
const root = path.dirname(dirname)

// Resolve local oroc binary if not on PATH
const repoRoot = path.resolve(root, '..')
const cli = resolveOrocExecutable(repoRoot)

const child = spawn(
  cli,
  [
    'build',
    '--headless',
    '--test=./index.js',
    '--platform=ios-simulator',
    '--allow-exec',
    '-r',
    '-o',
    '--env',
    'ORO_DEBUG_IPC'
  ],
  {
    stdio: 'inherit',
    cwd: root,
    env: {
      ...process.env,
      ORO_DEBUG_IPC: process.env.ORO_DEBUG_IPC || '1'
    }
  }
)

let retries = 1000
let fixturesReady = false

const interval = setInterval(() => {
  let container = null
  if (child.exitCode !== null || child.killed) {
    console.warn(
      'iOS Simulator exited before determining the container data directory'
    )
    clearInterval(interval)
    return
  }

  try {
    container = execSync(
      'xcrun simctl get_app_container booted computer.oro.runtime.tests data'
    )
    if (container) {
      container = container.toString().trim()
    }
  } catch (err) {
    if (--retries === 0) {
      clearInterval(interval)
      console.error(
        'Unable to determine the iOS Simulator container data directory:',
        err
      )
      process.exitCode = 1
      child.kill()
    }
  }

  if (!container) {
    return // continue
  }

  const TMPDIR = path.join(container, 'tmp')

  try {
    rm(path.join(TMPDIR, 'oro-test-fixtures'), {
      recursive: true,
      force: true
    })
  } catch {}

  cp(path.join(root, 'fixtures'), path.join(TMPDIR, 'oro-test-fixtures'), {
    recursive: true
  })

  fixturesReady = true
  clearInterval(interval)
}, 200)

child.once('error', (err) => {
  console.error('Unable to start the iOS Simulator test build:', err)
  process.exitCode = 1
  clearInterval(interval)
})

child.once('exit', (code, signal) => {
  clearInterval(interval)
  if (!fixturesReady) {
    console.error(
      'iOS Simulator test exited before its fixture directory was prepared'
    )
    process.exitCode = 1
  } else if (code !== 0) {
    console.error(
      `iOS Simulator test exited ${signal ? `from signal ${signal}` : `with code ${code}`}`
    )
    process.exitCode = code || 1
  }
})
