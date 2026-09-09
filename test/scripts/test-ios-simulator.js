import { rmSync as rm, cpSync as cp } from 'node:fs'
import { execFileSync, spawn } from 'node:child_process'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { resolveOrocExecutable } from './oroc-path.js'

const dirname = path.dirname(fileURLToPath(import.meta.url))
const root = path.dirname(dirname)
const simulator = process.env.ORO_IOS_SIMULATOR_UDID || 'booted'
const id = 'computer.oro.runtime.tests'

// Resolve local oroc binary if not on PATH
const repoRoot = path.resolve(root, '..')
const cli = resolveOrocExecutable(repoRoot)

execFileSync(
  cli,
  [
    'build',
    '--headless',
    '--test=./index.js',
    '--platform=ios-simulator',
    '--allow-exec',
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

const app = path.join(root, 'build', 'ios-simulator', 'oro-runtime-javascript-tests-dev.app')
execFileSync('xcrun', ['simctl', 'install', simulator, app], { stdio: 'inherit' })
const container = execFileSync('xcrun', ['simctl', 'get_app_container', simulator, id, 'data'], {
  encoding: 'utf8'
}).trim()
if (!path.isAbsolute(container)) {
  throw new Error('Unable to determine the iOS Simulator container data directory')
}

// Prepare fixtures before launch so the first test can read them immediately.
const fixtures = path.join(container, 'tmp', 'oro-test-fixtures')
rm(fixtures, { recursive: true, force: true })
cp(path.join(root, 'fixtures'), fixtures, { recursive: true })

let testExitCode = null
// The iOS runtime signals its launcher when output or a test result is ready.
process.on('SIGUSR1', () => {})
process.on('SIGUSR2', () => finish(0))
process.on('SIGTERM', () => finish(1))
process.on('SIGINT', () => finish(1))

const child = spawn('xcrun', ['simctl', 'launch', '--console', '--terminate-running-process', simulator, id], {
  stdio: 'inherit',
  env: {
    ...process.env,
    SIMCTL_CHILD_ORO_CLI_PID: String(process.pid)
  }
})

function finish (code) {
  testExitCode = testExitCode === 1 ? 1 : code
  child.kill()
}

child.once('error', (error) => {
  console.error('Unable to launch iOS Simulator tests:', error)
  process.exitCode = 1
})

child.once('exit', (code) => {
  // A successful simctl invocation alone does not prove that the tests finished.
  process.exitCode = testExitCode ?? (code || 1)
  if (process.exitCode !== 0) {
    console.error('iOS Simulator tests failed or exited before reporting a result')
  }
})
