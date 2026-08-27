import {
  rmSync as rm,
  cpSync as cp,
  existsSync,
  readFileSync,
  writeFileSync,
  mkdirSync
} from 'node:fs'
import { execFileSync } from 'node:child_process'
import path from 'node:path'
import os from 'node:os'
import { resolveOrocExecutable } from './oroc-path.js'

const dirname = path.dirname(
  import.meta.url.replace('file://', '').replace(/^\/[A-Za-z]:/, '')
)
const root = path.dirname(dirname)
const repoRoot = path.resolve(root, '..')

const RUNTIME_HOME_API = path.join(root, '..', 'api')
const CONFIG_FILENAMES = ['oro.toml', 'oro.ini']
const SKIP_EXT = '1'
const SKIP_TEST_EXT = '1'
const { DEBUG, TMP, TMPDIR = TMP || os.tmpdir() } = process.env

// Work directory copy where we can tweak oro.ini
const workdir = path.join(TMPDIR, 'oro-test-work-spa')
try {
  rm(workdir, { recursive: true, force: true })
} catch {}
mkdirSync(workdir, { recursive: true })
cp(root, workdir, { recursive: true })

// Enable SPA fallback for this run and set explicit default_index
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
    const add = (s) => (s.endsWith('\n') ? '' : '\n')
    const src = String(readFileSync(iniPath))
    const extra =
      '\n[webview]\nallow_any_route = true\ndefault_index = /router-spa/index.html\n'
    writeFileSync(iniPath, src + add(src) + extra)
  } catch (err) {
    console.warn(
      `Warning: failed to enable SPA fallback in ${filename}:`,
      err?.message || err
    )
  }
}

// Resolve local oroc binary if not on PATH
const cli = resolveOrocExecutable(repoRoot)

try {
  const env = {
    ...process.env,
    ORO_SKIP_DESKTOP_EXTENSION: SKIP_EXT,
    ORO_TEST_SKIP_TEST_EXTENSIONS: SKIP_TEST_EXT
  }
  if (!env.ORO_HOME_API) {
    env.ORO_HOME_API = RUNTIME_HOME_API
  }

  const args = ['build', '-r', '--test=./index-spa.js']
  if (!DEBUG) args.push('-o', '--prod')
  execFileSync(cli, args, {
    stdio: 'inherit',
    env,
    cwd: workdir
  })
} catch (err) {
  console.log({ err })
  if (process.platform === 'linux') {
    console.warn(
      'Hint: set ORO_SKIP_DESKTOP_EXTENSION=1 to skip building the desktop extension in CI environments.'
    )
  }
  console.warn(
    'Hint: set ORO_TEST_SKIP_TEST_EXTENSIONS=1 to skip building native test extensions in CI environments.'
  )
  process.exit(err.status || 1)
}
