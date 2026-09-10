import { execFileSync } from 'node:child_process'
import path from 'node:path'
import { resolveOrocExecutable } from './oroc-path.js'
import { stageAndroidFixtures } from './android-fixtures.js'

const { ANDROID_HOME, ORO_ANDROID_CI } = process.env
const dirname = path.dirname(import.meta.url.replace('file://', ''))
const root = path.dirname(dirname)
const adb = ANDROID_HOME
  ? path.join(
    ANDROID_HOME,
    'platform-tools',
    process.platform === 'win32' ? 'adb.exe' : 'adb'
  )
  : process.platform === 'win32' ? 'adb.exe' : 'adb'
const id = 'computer.oro.runtime.tests'

// Resolve local oroc binary if not on PATH
const repoRoot = path.resolve(root, '..')
const cli = resolveOrocExecutable(repoRoot)
const cliEnv = {
  ...process.env,
  ORO_TEST_FIXTURES_DIR: `/data/data/${id}/cache/oro-test-fixtures`,
  ORO_DEBUG_IPC: process.env.ORO_DEBUG_IPC || '1'
}

try {
  execFileSync(adb, ['uninstall', id], { stdio: 'inherit' })
} catch {}

if (ORO_ANDROID_CI) {
  execFileSync(
    cli,
    [
      'build',
      '-o',
      '--test=./index.js',
      '--headless',
      '--platform=android',
      '--allow-exec',
      '--env=CI',
      '--env=ORO_TEST_FIXTURES_DIR',
      '--env=ORO_ANDROID_CI'
    ],
    {
      stdio: 'inherit',
      env: cliEnv
    }
  )
} else {
  execFileSync(
    cli,
    [
      'build',
      '-o',
      '--test=./index.js',
      '--prod',
      '--headless',
      '--platform=android',
      '--allow-exec',
      '--env=ORO_TEST_FIXTURES_DIR',
      '--env',
      'ORO_DEBUG_IPC'
    ],
    {
      stdio: 'inherit',
      env: cliEnv
    }
  )
}

const flavor = ORO_ANDROID_CI ? 'dev' : 'live'
const apk = path.join(root, 'build', 'android', 'app', 'build', 'outputs', 'apk',
  flavor, 'debug', `app-${flavor}-debug.apk`)
// CI has no operator to answer runtime permission dialogs during startup.
const installArgs = ['install', '-r', ...(ORO_ANDROID_CI ? ['-g'] : []), apk]
execFileSync(adb, installArgs, { stdio: 'inherit' })

stageAndroidFixtures(adb, id, path.join(root, 'fixtures'))

execFileSync(
  process.env.SHELL || (process.platform === 'win32' ? 'bash' : 'sh'),
  [path.resolve(root, 'scripts', 'poll-adb-logcat.sh')],
  {
    stdio: 'inherit'
  }
)
