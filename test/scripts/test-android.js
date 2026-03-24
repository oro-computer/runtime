import { execFileSync, execSync as exec } from 'node:child_process'
import { existsSync } from 'node:fs'
import path from 'node:path'

const { ANDROID_HOME, ORO_ANDROID_CI, ORO_BIN } = process.env
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
const fixturesPath = '/data/local/tmp/oro-test-fixtures'

// Resolve local oroc binary if not on PATH
const repoRoot = path.resolve(root, '..')
const orocCandidate = path.join(
  repoRoot,
  'build',
  'x86_64-desktop',
  'bin',
  process.platform === 'win32' ? 'oroc.exe' : 'oroc'
)
const cli = ORO_BIN || (existsSync(orocCandidate) ? orocCandidate : 'oroc')

try {
  execFileSync(adb, ['uninstall', id], { stdio: 'inherit' })
} catch {}

if (ORO_ANDROID_CI) {
  exec(
    `${cli} build -r -o --test=./index.js --headless --platform=android --env=CI --env=ORO_ANDROID_CI`,
    {
      stdio: 'inherit'
    }
  )
} else {
  exec(
    `${cli} build -r -o --test=./index.js --prod --headless --platform=android --env ORO_DEBUG_IPC`,
    {
      stdio: 'inherit'
    }
  )
}

try {
  execFileSync(adb, ['shell', 'rm', '-rf', fixturesPath], {
    stdio: 'inherit'
  })
} catch {}

execFileSync(adb, ['push', path.join(root, 'fixtures'), fixturesPath], {
  stdio: 'inherit'
})

execFileSync(
  process.env.SHELL || (process.platform === 'win32' ? 'bash' : 'sh'),
  [path.resolve(root, 'scripts', 'poll-adb-logcat.sh')],
  {
    stdio: 'inherit'
  }
)
