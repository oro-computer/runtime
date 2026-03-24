import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)
const root = path.resolve(__dirname, '..')

function runCli (command, args, options = {}) {
  return spawnSync(command, args, {
    encoding: 'utf8',
    shell: process.platform === 'win32',
    ...options
  })
}

const runOroc = (args, options = {}) => runCli('oroc', args, options)
const MINIMAL_CONFIG = '[build]\nname = dotfiles-test\n'

function assert (cond, msg) {
  if (!cond) {
    console.error('Assertion failed:', msg)
    process.exit(1)
  }
}

// 1) oroc --prefix
{
  const { status, stdout } = runOroc(['--prefix'])
  assert(status === 0, 'oroc --prefix should exit 0')
  assert((stdout || '').trim().length > 0, 'oroc --prefix should print a path')
}

// 2) oroc --help includes usage/subcommands
{
  const { status, stdout } = runOroc(['--help'])
  assert(status === 0, 'oroc --help should exit 0')
  const out = (stdout || '').toLowerCase()
  assert(
    out.includes('usage') && out.includes('subcommands'),
    'oroc --help should include usage and subcommands'
  )
}

// 3) oroc -V --help (global short verbose)
{
  const { status } = runOroc(['-V', '--help'])
  assert(status === 0, 'oroc -V --help should exit 0')
}

// 4) oroc --unknown should fail with code 1
{
  const { status } = runOroc(['--definitely-unknown'])
  assert(status === 1, 'unknown top-level option should exit 1')
}

// 5) oroc print-build-dir with -V from test project
{
  const cwd = root
  const { status, stdout } = runOroc(
    ['print-build-dir', '--platform=linux', '-V'],
    { cwd }
  )
  assert(status === 0, 'print-build-dir should exit 0')
  assert(
    (stdout || '').trim().length > 0,
    'print-build-dir should print a path'
  )
}

// 6) oroc -D --help (global short debug)
{
  const { status } = runOroc(['-D', '--help'])
  assert(status === 0, 'oroc -D --help should exit 0')
}

// 7) Dotfile env injection (.ororc)
{
  const projectRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-dotfiles-'))
  try {
    const oroConfig = path.join(projectRoot, 'oro.ini')
    fs.writeFileSync(oroConfig, MINIMAL_CONFIG, 'utf8')
    const preferredRc = path.join(projectRoot, '.ororc')
    fs.writeFileSync(preferredRc, 'env_RC_SOURCE = oro\n', 'utf8')

    const preferredResult = runOroc(['env'], { cwd: projectRoot })
    assert(preferredResult.status === 0, 'oroc env with .ororc should exit 0')
    assert(
      preferredResult.stdout.includes('RC_SOURCE=oro'),
      '.ororc should set env vars'
    )
  } finally {
    fs.rmSync(projectRoot, { recursive: true, force: true })
  }
}

console.log('cli tests passed')
