import {
  rmSync as rm,
  cpSync as cp,
  existsSync,
  readFileSync,
  writeFileSync,
  mkdirSync
} from 'node:fs'
import { execSync as exec } from 'node:child_process'
import path from 'node:path'
import os from 'node:os'

const dirname = path.dirname(
  import.meta.url.replace('file://', '').replace(/^\/[A-Za-z]:/, '')
)
const root = path.dirname(dirname)
const repoRoot = path.resolve(root, '..')

const RUNTIME_HOME_API = path.join(root, '..', 'api')
const CONFIG_FILENAMES = ['oro.toml', 'oro.ini']
const SKIP_EXT = '1'
const SKIP_TEST_EXT = '1'
const { DEBUG, TMP, TMPDIR = TMP || os.tmpdir(), ORO_BIN } = process.env

const workdir = path.join(TMPDIR, 'oro-test-work-autoindex')
try {
  rm(workdir, { recursive: true, force: true })
} catch {}
mkdirSync(workdir, { recursive: true })
cp(root, workdir, { recursive: true })

if (SKIP_TEST_EXT) {
  for (const filename of CONFIG_FILENAMES) {
    const iniPath = path.join(workdir, filename)
    if (!existsSync(iniPath)) continue
    try {
      const src = String(readFileSync(iniPath))
      const scrub = (input) =>
        input
          .replace(/\n\[build\.extensions\][\s\S]*?(?=\n\[|$)/g, '\n')
          .replace(/\n\[build\.extensions\.linux\][\s\S]*?(?=\n\[|$)/g, '\n')
          .replace(/\n\[build\.extensions\.mac\][\s\S]*?(?=\n\[|$)/g, '\n')
          .replace(/\n\[build\.extensions\.win\][\s\S]*?(?=\n\[|$)/g, '\n')
      writeFileSync(iniPath, scrub(src))
    } catch (err) {
      console.warn(
        `Warning: failed to scrub extensions from ${filename}:`,
        err?.message || err
      )
    }
  }
}

for (const filename of CONFIG_FILENAMES) {
  const iniPath = path.join(workdir, filename)
  if (!existsSync(iniPath)) continue
  try {
    const src = String(readFileSync(iniPath))
    const add = src.endsWith('\n') ? '' : '\n'
    const extra = '\n[webview]\nautoindex = true\n'
    writeFileSync(iniPath, src + add + extra)
  } catch (err) {
    console.warn(
      `Warning: failed to enable autoindex in ${filename}:`,
      err?.message || err
    )
  }
}

const orocCandidate = path.join(
  repoRoot,
  'build',
  'x86_64-desktop',
  'bin',
  process.platform === 'win32' ? 'oroc.exe' : 'oroc'
)
const cli = ORO_BIN || (existsSync(orocCandidate) ? orocCandidate : 'oroc')

try {
  const env = {
    ...process.env,
    ORO_SKIP_DESKTOP_EXTENSION: SKIP_EXT,
    ORO_TEST_SKIP_TEST_EXTENSIONS: SKIP_TEST_EXT,
    ORO_TEST_AUTOINDEX: '1'
  }
  if (!env.ORO_HOME_API) {
    env.ORO_HOME_API = RUNTIME_HOME_API
  }

  exec(
    `${cli} build -r --test=./index-autoindex.js ${!DEBUG ? '-o --prod' : ''}`,
    {
      stdio: 'inherit',
      env,
      cwd: workdir
    }
  )
} catch (err) {
  console.log({ err })
  if (process.platform === 'linux') {
    console.warn(
      'Hint: set ORO_TEST_SKIP_DESKTOP_EXTENSION=1 to skip building the desktop extension in CI environments.'
    )
  }
  console.warn(
    'Hint: set ORO_TEST_SKIP_TEST_EXTENSIONS=1 to skip building native test extensions in CI environments.'
  )
  process.exit(err.status || 1)
}
