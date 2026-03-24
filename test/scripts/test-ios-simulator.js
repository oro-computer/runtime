import { rmSync as rm, cpSync as cp, existsSync } from 'node:fs'
import { execSync, spawn } from 'node:child_process'
import path from 'node:path'

const dirname = path.dirname(
  import.meta.url.replace('file://', '').replace(/^\/[A-Za-z]:/, '')
)
const root = path.dirname(dirname)

// Resolve local oroc binary if not on PATH
const repoRoot = path.resolve(root, '..')
const orocCandidate = path.join(
  repoRoot,
  'build',
  'x86_64-desktop',
  'bin',
  process.platform === 'win32' ? 'oroc.exe' : 'oroc'
)
const { ORO_BIN } = process.env
const cli = ORO_BIN || (existsSync(orocCandidate) ? orocCandidate : 'oroc')

const child = spawn(
  cli,
  [
    'build',
    '--headless',
    '--test=./index.js',
    '--platform=ios-simulator',
    '-r',
    '-o',
    '--env',
    'ORO_DEBUG_IPC'
  ],
  {
    stdio: 'inherit'
  }
)

let retries = 1000

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
      throw err
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

  clearInterval(interval)
}, 200)
