import {
  rmSync as rm,
  cpSync as cp,
  existsSync,
  readFileSync,
  writeFileSync,
  mkdirSync,
  symlinkSync,
  accessSync,
  constants as fsConstants
} from 'node:fs'
import { spawnSync } from 'node:child_process'
import path from 'node:path'
import os from 'node:os'
import { fileURLToPath } from 'node:url'
import { parseArgs } from 'node:util'
import { resolveOrocExecutable } from './oroc-path.js'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)
const testRoot = path.dirname(__dirname)
const repoRoot = path.resolve(testRoot, '..')
const RUNTIME_HOME_API = path.join(testRoot, '..', 'api')

const DEFAULT_FIXTURES_DIRNAME = 'oro-test-fixtures'
const DEFAULT_WORKDIR_NAME = 'oro-test-work'
const DEFAULT_STRICT_WORKDIR_NAME = 'oro-test-work-lifecycle'
const CONFIG_BASENAMES = ['oro.toml', 'oro.ini']
const RUNTIME_ENV_KEYS = [
  'PWD',
  'TMP',
  'TEMP',
  'TMPDIR',
  'HOME',
  'XDG_DATA_HOME',
  'XDG_CONFIG_HOME',
  'XDG_CACHE_HOME',
  'XDG_STATE_HOME',
  'ORO_TEST_FIXTURES_DIR',
  'ORO_TEST_GREP',
  'ORO_TEST_SKIP',
  'ORO_TEST_ONLY'
]

function forEachConfigFile (dir, visitor) {
  for (const basename of CONFIG_BASENAMES) {
    const configPath = path.join(dir, basename)
    if (existsSync(configPath)) {
      visitor(configPath)
    }
  }
}

export function runDesktopTests (inputOptions = {}) {
  const options = resolveOptions(inputOptions)
  const log = options.verbose
    ? (...args) => console.log('[desktop]', ...args)
    : () => {}

  log(
    `entry=${options.entry} strict=${options.strict ? 'on' : 'off'} mode=${options.isolate ? 'isolated' : 'in-place'}${options.quick ? ' (quick)' : ''}`
  )

  const fixturesDir = prepareFixtures(options, log)
  const workdirState = prepareWorkdir(options, log)
  const entryPath = path.isAbsolute(options.entry)
    ? options.entry
    : path.resolve(workdirState.path, 'src', options.entry)
  if (!existsSync(entryPath)) {
    console.error(
      `Test entry does not exist: ${options.entry} (resolved to ${entryPath})`
    )
    workdirState.cleanup()
    return 1
  }

  const childEnv = {
    ...process.env,
    ...options.env
  }

  childEnv.ORO_RUNTIME_NODE_PATH ??= path.join(
    repoRoot,
    'npm/packages/@oro-computer/runtime-node/index.js'
  )

  if (!childEnv.ORO_HOME_API) {
    childEnv.ORO_HOME_API = RUNTIME_HOME_API
  }

  applyLinuxTestEnv(childEnv, options.tmpdir, options.headless)

  if (options.skipDesktopExtension) {
    childEnv.ORO_SKIP_DESKTOP_EXTENSION = '1'
  }

  if (options.skipTestExtensions) {
    childEnv.ORO_TEST_SKIP_TEST_EXTENSIONS = '1'
  }

  childEnv.ORO_TEST_FIXTURES_DIR = fixturesDir

  const oroc = resolveOrocExecutable(repoRoot, childEnv)
  const buildArgs = createBuildArgs(options, fixturesDir, childEnv)

  const firstPass = runOroc(oroc, buildArgs, childEnv, workdirState.path)
  if (firstPass.status !== 0) {
    printFailureHints(options)
    workdirState.cleanup()
    return firstPass.status || 1
  }

  if (options.strict) {
    const strictResult = runStrictPass({
      options,
      baseEnv: childEnv,
      oroc,
      buildArgs,
      sourceWorkdir: workdirState.path,
      log
    })

    if (strictResult.status !== 0) {
      workdirState.cleanup()
      return strictResult.status || 1
    }
  }

  workdirState.cleanup()
  return 0
}

export default runDesktopTests

if (fileURLToPath(import.meta.url) === process.argv[1]) {
  const { options, passthrough } = parseCli()
  const exitCode = runDesktopTests({ ...options, orocArgs: passthrough })
  process.exit(exitCode ?? 0)
}

function resolveOptions (input = {}) {
  const env = process.env
  const tmpdir =
    input.tmpdir || env.ORO_TEST_TMPDIR || env.TMPDIR || env.TMP || os.tmpdir()

  const quick = pickBoolean(input.quick, env.ORO_TEST_QUICK)
  const debug = pickBoolean(input.debug, env.ORO_TEST_DEBUG, env.DEBUG, false)

  let strict = pickBoolean(input.strict, env.ORO_TEST_STRICT)
  if (strict === undefined) {
    const skipStrict = pickBoolean(
      env.ORO_SKIP_LIFECYCLE_STRICT,
      env.ORO_TEST_NO_STRICT
    )
    strict = skipStrict === undefined ? true : !skipStrict
  }
  if (
    quick &&
    input.strict === undefined &&
    env.ORO_TEST_STRICT === undefined &&
    env.ORO_SKIP_LIFECYCLE_STRICT === undefined
  ) {
    strict = false
  }

  let skipDesktopExtension = pickBoolean(
    input.skipDesktopExtension,
    env.ORO_TEST_SKIP_DESKTOP_EXTENSION,
    false
  )
  if (
    quick &&
    input.skipDesktopExtension === undefined &&
    env.ORO_TEST_SKIP_DESKTOP_EXTENSION === undefined
  ) {
    skipDesktopExtension = true
  }

  let skipTestExtensions = pickBoolean(
    input.skipTestExtensions,
    env.ORO_TEST_SKIP_TEST_EXTENSIONS,
    false
  )
  if (
    quick &&
    input.skipTestExtensions === undefined &&
    env.ORO_TEST_SKIP_TEST_EXTENSIONS === undefined
  ) {
    skipTestExtensions = true
  }

  let isolate = pickBoolean(input.isolate, env.ORO_TEST_ISOLATE)
  if (isolate === undefined) {
    isolate = skipTestExtensions
  }
  if (
    quick &&
    input.isolate === undefined &&
    env.ORO_TEST_ISOLATE === undefined
  ) {
    isolate = true
  }

  let reuseWorkdir = pickBoolean(input.reuseWorkdir, env.ORO_TEST_REUSE_WORKDIR)
  if (reuseWorkdir === undefined) {
    reuseWorkdir = !!quick
  }

  const refreshWorkdir = pickBoolean(
    input.refreshWorkdir,
    env.ORO_TEST_REFRESH_WORKDIR,
    false
  )
  let keepWorkdir = pickBoolean(input.keepWorkdir, env.ORO_TEST_KEEP_WORKDIR)
  if (keepWorkdir === undefined) {
    keepWorkdir = reuseWorkdir
  }

  let reuseFixtures = pickBoolean(
    input.reuseFixtures,
    env.ORO_TEST_REUSE_FIXTURES
  )
  if (reuseFixtures === undefined) {
    reuseFixtures = reuseWorkdir
  }
  const refreshFixtures = pickBoolean(
    input.refreshFixtures,
    env.ORO_TEST_REFRESH_FIXTURES,
    false
  )

  const headless = pickBoolean(input.headless, env.ORO_TEST_HEADLESS)

  let prod = pickBoolean(input.prod, env.ORO_TEST_PROD)
  if (prod === undefined) {
    prod = !debug
  }

  const workdir =
    input.workdir ||
    env.ORO_TEST_WORKDIR ||
    path.join(tmpdir, DEFAULT_WORKDIR_NAME)
  const fixturesDir =
    input.fixturesDir ||
    env.ORO_TEST_FIXTURES_DIR ||
    path.join(tmpdir, DEFAULT_FIXTURES_DIRNAME)

  const entry = normalizeEntry(
    input.entry || env.ORO_TEST_ENTRY || './index.js'
  )
  const verbose = pickBoolean(input.verbose, env.ORO_TEST_VERBOSE, false)

  const orocArgs = []
  if (Array.isArray(input.orocArgs)) {
    orocArgs.push(...input.orocArgs)
  }
  if (env.ORO_TEST_OROC_ARGS) {
    orocArgs.push(...parseArgList(env.ORO_TEST_OROC_ARGS))
  }

  return {
    entry,
    strict,
    debug,
    skipDesktopExtension,
    skipTestExtensions,
    isolate,
    reuseWorkdir,
    refreshWorkdir,
    keepWorkdir,
    reuseFixtures,
    refreshFixtures,
    headless,
    prod,
    tmpdir,
    workdir,
    fixturesDir,
    env: input.env,
    verbose,
    quick: !!quick,
    orocArgs
  }
}

function prepareFixtures (options, log) {
  const fixturesSource = path.join(testRoot, 'fixtures')
  const fixturesTarget = options.fixturesDir
  const shouldCopy =
    options.refreshFixtures ||
    !options.reuseFixtures ||
    !existsSync(fixturesTarget)

  if (shouldCopy) {
    try {
      rm(fixturesTarget, { recursive: true, force: true })
    } catch {}
    mkdirSync(path.dirname(fixturesTarget), { recursive: true })
    cp(fixturesSource, fixturesTarget, { recursive: true })
    log(`fixtures refreshed → ${fixturesTarget}`)
  } else {
    log(`fixtures reused → ${fixturesTarget}`)
  }

  return fixturesTarget
}

function prepareWorkdir (options, log) {
  if (!options.isolate) {
    if (options.skipTestExtensions) {
      console.warn(
        'Warning: --skip-test-extensions ignored when running in-place. Use --isolate to stage a copy of the test app.'
      )
    }
    return {
      path: testRoot,
      cleanup: () => {}
    }
  }

  const workdir = options.workdir
  if (options.refreshWorkdir || !options.reuseWorkdir) {
    try {
      rm(workdir, { recursive: true, force: true })
    } catch {}
  }

  if (!existsSync(workdir)) {
    mkdirSync(workdir, { recursive: true })
    copyTestApp(workdir)
    log(`workdir created → ${workdir}`)
  } else if (!options.reuseWorkdir || options.refreshWorkdir) {
    copyTestApp(workdir)
    log(`workdir synced → ${workdir}`)
  } else {
    copyTestApp(workdir)
    log(`workdir sources updated → ${workdir}`)
  }

  if (options.skipTestExtensions) {
    forEachConfigFile(workdir, scrubExtensions)
  }

  const cleanup = () => {
    if (options.keepWorkdir) return
    if (options.reuseWorkdir) return
    try {
      rm(workdir, { recursive: true, force: true })
    } catch {}
  }

  return {
    path: workdir,
    cleanup
  }
}

function copyTestApp (workdir) {
  cp(testRoot, workdir, {
    recursive: true,
    filter: (source) => {
      const relative = path.relative(testRoot, source)
      const [topLevel] = relative.split(path.sep)
      return !['build', 'node_modules', 'tmp'].includes(topLevel)
    }
  })

  const packageScope = path.join(workdir, 'node_modules', '@oro-computer')
  const runtimeModule = path.join(packageScope, 'runtime')
  mkdirSync(packageScope, { recursive: true })
  if (!existsSync(runtimeModule)) {
    symlinkSync(RUNTIME_HOME_API, runtimeModule, 'dir')
  }
}

function scrubExtensions (iniPath) {
  try {
    const src = existsSync(iniPath) ? String(readFileSync(iniPath)) : ''
    const scrubbed = src
      .replace(/\n\[build\.extensions\][\s\S]*?(?=\n\[|$)/g, '\n')
      .replace(/\n\[build\.extensions\.linux\][\s\S]*?(?=\n\[|$)/g, '\n')
      .replace(/\n\[build\.extensions\.mac\][\s\S]*?(?=\n\[|$)/g, '\n')
      .replace(/\n\[build\.extensions\.win\][\s\S]*?(?=\n\[|$)/g, '\n')
    if (scrubbed !== src) {
      writeFileSync(iniPath, scrubbed)
    }
  } catch (err) {
    console.warn(
      `Warning: failed to scrub extensions from ${path.basename(iniPath)}:`,
      err?.message || err
    )
  }
}

function createBuildArgs (options, fixturesDir, runtimeEnv) {
  const args = [
    'build',
    '-r',
    '--test',
    options.entry
  ]
  runtimeEnv.ORO_TEST_FIXTURES_DIR = fixturesDir
  for (const name of RUNTIME_ENV_KEYS) {
    if (runtimeEnv[name] !== undefined) {
      args.push(`--env=${name}=${runtimeEnv[name]}`)
    }
  }
  if (options.prod) {
    args.push('-o', '--prod')
  }
  if (options.headless) {
    args.push('--headless')
  }
  if (Array.isArray(options.orocArgs) && options.orocArgs.length > 0) {
    args.push(...options.orocArgs)
  }
  return args
}

function runStrictPass ({
  options,
  baseEnv,
  oroc,
  buildArgs,
  sourceWorkdir,
  log
}) {
  const strictWorkdir = path.join(options.tmpdir, DEFAULT_STRICT_WORKDIR_NAME)
  try {
    rm(strictWorkdir, { recursive: true, force: true })
  } catch {}
  cp(sourceWorkdir, strictWorkdir, { recursive: true })

  const appendLifecycleFlag = (iniPath) => {
    try {
      const src = existsSync(iniPath) ? String(readFileSync(iniPath)) : ''
      const add = src.endsWith('\n') ? '' : '\n'
      writeFileSync(
        iniPath,
        src + add + 'lifecycle_desktop_always_running = false\n'
      )
    } catch (err) {
      console.warn(
        `Warning: failed to set lifecycle_desktop_always_running in ${path.basename(iniPath)}:`,
        err?.message || err
      )
    }
  }
  forEachConfigFile(strictWorkdir, appendLifecycleFlag)

  const env = {
    ...baseEnv,
    ORO_LIFECYCLE_TEST_STRICT: '1'
  }

  const result = runOroc(oroc, buildArgs, env, strictWorkdir)

  if (result.status === 0) {
    log('strict lifecycle pass complete')
  }

  if (!options.keepWorkdir) {
    try {
      rm(strictWorkdir, { recursive: true, force: true })
    } catch {}
  }
  return result
}

function runOroc (oroc, args, env, cwd) {
  const result = spawnSync(oroc, args, {
    stdio: 'inherit',
    env,
    cwd
  })

  if (result.error) {
    console.error(result.error)
  }

  return {
    status: result.status ?? (result.error ? 1 : 0)
  }
}

function applyLinuxTestEnv (env, tmpdir, headless) {
  if (process.platform !== 'linux') return

  if (headless) {
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
  }

  if (!isWritableDirectory(env.XDG_RUNTIME_DIR)) {
    env.XDG_RUNTIME_DIR = path.join(tmpdir, 'oro-xdg-runtime')
  }

  const xdgHomes = {
    XDG_DATA_HOME: 'data',
    XDG_CONFIG_HOME: 'config',
    XDG_CACHE_HOME: 'cache',
    XDG_STATE_HOME: 'state'
  }

  for (const [name, basename] of Object.entries(xdgHomes)) {
    if (!env[name]) {
      env[name] = path.join(tmpdir, 'oro-xdg', basename)
    }
    mkdirSync(env[name], { recursive: true })
  }

  if (!env.TMPDIR) {
    env.TMPDIR = tmpdir
  }

  mkdirSync(env.XDG_RUNTIME_DIR, {
    recursive: true,
    mode: 0o700
  })
}

function isWritableDirectory (directory) {
  if (!directory) return false

  try {
    mkdirSync(directory, { recursive: true })
    accessSync(directory, fsConstants.W_OK)
    return true
  } catch {
    return false
  }
}

function printFailureHints (options) {
  if (!options.skipDesktopExtension && process.platform === 'linux') {
    console.warn(
      'Hint: use --skip-desktop-extension (or set ORO_TEST_SKIP_DESKTOP_EXTENSION=1) to skip building the desktop extension in constrained environments.'
    )
  }
  if (!options.skipTestExtensions) {
    console.warn(
      'Hint: use --skip-test-extensions (or set ORO_TEST_SKIP_TEST_EXTENSIONS=1) to avoid building native test extensions.'
    )
  }
  if (!options.quick) {
    console.warn(
      'Hint: add --quick for a faster iteration mode (skips strict lifecycle pass and reuses staged assets).'
    )
  }
}

function normalizeEntry (entry) {
  if (!entry) return './index.js'
  if (entry.startsWith('./src/')) return `./${entry.slice('./src/'.length)}`
  if (entry.startsWith('src/')) return `./${entry.slice('src/'.length)}`
  if (entry.startsWith('./') || entry.startsWith('../')) return entry
  if (entry.startsWith('/')) return entry
  return `./${entry}`
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
      entry: { type: 'string' },
      strict: { type: 'boolean' },
      'no-strict': { type: 'boolean' },
      quick: { type: 'boolean' },
      isolate: { type: 'boolean' },
      'no-isolate': { type: 'boolean' },
      'reuse-workdir': { type: 'boolean' },
      'refresh-workdir': { type: 'boolean' },
      'keep-workdir': { type: 'boolean' },
      'reuse-fixtures': { type: 'boolean' },
      'refresh-fixtures': { type: 'boolean' },
      'skip-test-extensions': { type: 'boolean' },
      'skip-desktop-extension': { type: 'boolean' },
      debug: { type: 'boolean' },
      prod: { type: 'boolean' },
      'no-prod': { type: 'boolean' },
      headless: { type: 'boolean' },
      workdir: { type: 'string' },
      'fixtures-dir': { type: 'string' },
      tmpdir: { type: 'string' },
      verbose: { type: 'boolean' },
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

  let entry = values.entry
  const passthrough = values['oroc-arg'] ? [...values['oroc-arg']] : []

  if (!entry && positionals.length > 0) {
    entry = positionals[0]
    passthrough.push(...positionals.slice(1))
  } else if (positionals.length > 0) {
    passthrough.push(...positionals)
  }

  return {
    options: {
      entry,
      strict: values['no-strict'] ? false : values.strict,
      quick: values.quick,
      isolate: values['no-isolate'] ? false : values.isolate,
      reuseWorkdir: values['reuse-workdir'],
      refreshWorkdir: values['refresh-workdir'],
      keepWorkdir: values['keep-workdir'],
      reuseFixtures: values['reuse-fixtures'],
      refreshFixtures: values['refresh-fixtures'],
      skipTestExtensions: values['skip-test-extensions'],
      skipDesktopExtension: values['skip-desktop-extension'],
      debug: values.debug,
      prod: values['no-prod'] ? false : values.prod,
      headless: values.headless,
      workdir: values.workdir,
      fixturesDir: values['fixtures-dir'],
      tmpdir: values.tmpdir,
      verbose: values.verbose
    },
    passthrough
  }
}

function printHelp () {
  console.log(`Usage: node scripts/test-desktop.js [options] [entry]

Options:
  --entry <file>               Test entry file (defaults to ./index.js)
  --quick                      Enable fast iteration mode (reuse staged assets, skip strict pass)
  --strict / --no-strict       Control lifecycle strict pass
  --isolate / --no-isolate     Stage tests into a tmp workdir (defaults to on when skipping extensions)
  --reuse-workdir              Reuse staged workdir instead of wiping it each run
  --refresh-workdir            Force refresh of the staged workdir
  --keep-workdir               Keep the staged workdir after completion
  --skip-test-extensions       Skip building native test extensions (sqlite/runtime-core)
  --skip-desktop-extension     Skip rebuilding the desktop runtime extension
  --reuse-fixtures             Reuse copied fixtures between runs
  --refresh-fixtures           Force refresh of copied fixtures
  --debug                      Run without release optimisation flags
  --prod / --no-prod           Force production build flags
  --headless                   Forward --headless to oroc
  --workdir <path>             Custom isolated workdir directory
  --fixtures-dir <path>        Custom fixtures copy directory
  --tmpdir <path>              Base tmp directory for staging
  --oroc-arg <value>           Additional argument forwarded to oroc (repeatable)
  --verbose                    Print detailed runner diagnostics
  -h, --help                   Show this message`)
}
