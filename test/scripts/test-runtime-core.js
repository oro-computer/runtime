import { spawnSync } from 'node:child_process'
import { existsSync, mkdirSync } from 'node:fs'
import path from 'node:path'
import os from 'node:os'
import { fileURLToPath } from 'node:url'
import { parseArgs } from 'node:util'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)
const testRoot = path.dirname(__dirname)
const repoRoot = path.resolve(testRoot, '..')

export function runRuntimeCoreTests (inputOptions = {}) {
  const options = resolveOptions(inputOptions)

  const env = {
    ...process.env,
    ...options.env
  }

  if (options.skipDesktopExtension) {
    env.ORO_SKIP_DESKTOP_EXTENSION = '1'
  }

  applyLinuxHeadlessEnv(env, options.tmpdir, options.headless)

  const oroc = resolveOrocExecutable(env)
  const args = createArgs(options)
  const result = spawnSync(oroc, args, {
    stdio: 'inherit',
    env,
    cwd: options.cwd || testRoot
  })

  if (
    result.status &&
    !options.skipDesktopExtension &&
    process.platform === 'linux'
  ) {
    console.warn(
      'Hint: use --skip-desktop-extension (or set ORO_TEST_SKIP_DESKTOP_EXTENSION=1) to skip building the desktop extension in constrained environments.'
    )
  }

  return result.status || 0
}

export default runRuntimeCoreTests

if (process.argv[1] === __filename) {
  const { options, passthrough } = parseCli()
  const code = runRuntimeCoreTests({ ...options, orocArgs: passthrough })
  process.exit(code)
}

function resolveOptions (input = {}) {
  const env = process.env

  const skipDesktopExtension = pickBoolean(
    input.skipDesktopExtension,
    env.ORO_TEST_SKIP_DESKTOP_EXTENSION,
    false
  )
  let headless = pickBoolean(input.headless, env.ORO_TEST_HEADLESS, true)
  if (headless === undefined) headless = true
  const tmpdir =
    input.tmpdir || env.ORO_TEST_TMPDIR || env.TMPDIR || env.TMP || os.tmpdir()

  const orocArgs = []
  if (Array.isArray(input.orocArgs)) {
    orocArgs.push(...input.orocArgs)
  }
  if (env.ORO_TEST_RUNTIME_CORE_OROC_ARGS) {
    orocArgs.push(...parseArgList(env.ORO_TEST_RUNTIME_CORE_OROC_ARGS))
  }

  return {
    skipDesktopExtension,
    headless,
    tmpdir,
    env: input.env,
    cwd: input.cwd,
    orocArgs
  }
}

function resolveOrocExecutable (env) {
  if (env.ORO_BIN) return env.ORO_BIN

  const buildDir = path.join(repoRoot, 'build', 'x86_64-desktop', 'bin')
  const candidates = [
    path.join(buildDir, process.platform === 'win32' ? 'oroc.exe' : 'oroc'),
    'oroc'
  ]

  for (const candidate of candidates) {
    const resolved = resolveExecutableCandidate(candidate)
    if (resolved) return resolved
  }

  return 'oroc'
}

function resolveExecutableCandidate (candidate) {
  if (path.isAbsolute(candidate)) {
    return existsSync(candidate) ? candidate : null
  }

  const whichCmd = process.platform === 'win32' ? 'where' : 'which'
  const result = spawnSync(whichCmd, [candidate], { stdio: 'ignore' })
  if (result.status === 0) {
    return candidate
  }
  return null
}

function createArgs (options) {
  const args = [
    'build',
    '--run',
    '--only-build',
    '--test',
    'runtime-core/main.js'
  ]
  if (options.headless) {
    args.push('--headless')
  }
  if (Array.isArray(options.orocArgs) && options.orocArgs.length > 0) {
    args.push(...options.orocArgs)
  }
  return args
}

function applyLinuxHeadlessEnv (env, tmpdir, headless) {
  if (process.platform !== 'linux' || !headless) return

  if (!env.LIBGL_ALWAYS_SOFTWARE) {
    env.LIBGL_ALWAYS_SOFTWARE = '1'
  }

  if (!env.WEBKIT_DISABLE_COMPOSITING_MODE) {
    env.WEBKIT_DISABLE_COMPOSITING_MODE = '1'
  }

  if (!env.GDK_BACKEND) {
    env.GDK_BACKEND = 'x11'
  }

  if (!env.GSK_RENDERER) {
    env.GSK_RENDERER = 'cairo'
  }

  if (!env.XDG_RUNTIME_DIR) {
    env.XDG_RUNTIME_DIR = path.join(tmpdir, 'oro-xdg-runtime')
  }

  mkdirSync(env.XDG_RUNTIME_DIR, {
    recursive: true,
    mode: 0o700
  })
}

function pickBoolean (...candidates) {
  for (const candidate of candidates) {
    const parsed = parseOptionalBoolean(candidate)
    if (parsed !== undefined) return parsed
  }
  return undefined
}

function parseOptionalBoolean (value) {
  if (value === undefined || value === null) return undefined
  if (typeof value === 'boolean') return value
  if (typeof value === 'number') return value !== 0
  if (typeof value === 'string') {
    const trimmed = value.trim().toLowerCase()
    if (
      trimmed === '1' ||
      trimmed === 'true' ||
      trimmed === 'yes' ||
      trimmed === 'on'
    ) {
      return true
    }
    if (
      trimmed === '0' ||
      trimmed === 'false' ||
      trimmed === 'no' ||
      trimmed === 'off'
    ) {
      return false
    }
    return trimmed.length > 0
  }
  return undefined
}

function parseArgList (value) {
  if (!value) return []
  const matches = value.match(/"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'|\S+/g) || []
  return matches.map((segment) => {
    if (
      (segment.startsWith('"') && segment.endsWith('"')) ||
      (segment.startsWith("'") && segment.endsWith("'"))
    ) {
      return segment.slice(1, -1)
    }
    return segment
  })
}

function parseCli (argv = process.argv.slice(2)) {
  const { values, positionals } = parseArgs({
    options: {
      'skip-desktop-extension': { type: 'boolean' },
      headless: { type: 'boolean' },
      'oroc-arg': { type: 'string', multiple: true },
      help: { type: 'boolean', short: 'h' }
    },
    allowPositionals: true,
    argv
  })

  if (values.help) {
    printHelp()
    process.exit(0)
  }

  const passthrough = values['oroc-arg'] ? [...values['oroc-arg']] : []
  if (positionals.length > 0) {
    passthrough.push(...positionals)
  }

  return {
    options: {
      skipDesktopExtension: values['skip-desktop-extension'],
      headless: values.headless
    },
    passthrough
  }
}

function printHelp () {
  console.log(`Usage: node scripts/test-runtime-core.js [options]

Options:
  --skip-desktop-extension     Skip desktop extension build (ORO_SKIP_DESKTOP_EXTENSION=1)
  --no-headless                Run tests without --headless
  --oroc-arg <value>           Additional argument forwarded to oroc (repeatable)
  -h, --help                   Show this message`)
}
