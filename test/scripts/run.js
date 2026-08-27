import { spawnSync } from 'node:child_process'
import { copyFileSync, existsSync, readFileSync, mkdirSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { parseArgs } from 'node:util'

import runDesktopTests from './test-desktop.js'
import runRuntimeCoreTests from './test-runtime-core.js'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)
const testRoot = path.dirname(__dirname)
const repoRoot = path.resolve(testRoot, '..')
const isMain = process.argv[1] && path.resolve(process.argv[1]) === __filename

const FALLBACK_TARGETS = new Map([
  ['desktop-autoindex', 'test:desktop-autoindex'],
  ['desktop-spa', 'test:desktop-spa'],
  ['android', 'test:android'],
  ['android-emulator', 'test:android-emulator'],
  ['ios-simulator', 'test:ios-simulator'],
  ['cli', 'test:cli'],
  ['cli-ip', 'test:cli-ip'],
  ['node', 'test:node']
])

const DESKTOP_TARGETS = new Set(['desktop'])
const RUNTIME_CORE_TARGETS = new Set(['runtime-core'])

const DEFAULT_TARGET = 'desktop'

async function main () {
  const { values, positionals } = parseArgs({
    options: {
      target: { type: 'string', short: 't' },
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
      match: { type: 'string' },
      skip: { type: 'string' },
      only: { type: 'string' },
      install: { type: 'boolean' },
      'skip-install': { type: 'boolean' },
      'force-install': { type: 'boolean' },
      'list-targets': { type: 'boolean' },
      verbose: { type: 'boolean' },
      'oroc-arg': { type: 'string', multiple: true },
      help: { type: 'boolean', short: 'h' }
    },
    allowPositionals: true
  })

  if (values.help) {
    printHelp()
    process.exit(0)
  }

  if (values['list-targets']) {
    listTargets()
    process.exit(0)
  }

  let target = values.target
  const passthrough = values['oroc-arg'] ? [...values['oroc-arg']] : []

  if (!target && positionals.length > 0) {
    target = positionals[0]
    passthrough.push(...positionals.slice(1))
  } else if (positionals.length > 0) {
    passthrough.push(...positionals)
  }

  target = target || DEFAULT_TARGET

  const envOverrides = collectEnvOverrides({
    match: values.match,
    skip: values.skip,
    only: values.only
  })

  Object.assign(process.env, envOverrides)

  syncEnvFile()

  const childEnv = {
    ...process.env,
    ...envOverrides
  }

  ensureDependencies({
    skipInstall: values['skip-install'],
    forceInstall: values.install || values['force-install'],
    verbose: values.verbose,
    env: childEnv
  })

  let exitCode = 0

  if (DESKTOP_TARGETS.has(target)) {
    exitCode = runDesktopTests({
      entry: values.entry,
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
      verbose: values.verbose,
      env: childEnv,
      orocArgs: passthrough
    })
  } else if (RUNTIME_CORE_TARGETS.has(target)) {
    exitCode = runRuntimeCoreTests({
      skipDesktopExtension: values['skip-desktop-extension'],
      headless: values.headless,
      env: childEnv,
      cwd: testRoot,
      orocArgs: passthrough
    })
  } else if (FALLBACK_TARGETS.has(target)) {
    exitCode = runFallbackTarget(target, childEnv, values.verbose)
  } else {
    console.error(
      `Unknown test target "${target}". Use --list-targets for supported values.`
    )
    process.exit(1)
  }

  process.exit(exitCode || 0)
}

if (isMain) {
  main().catch((err) => {
    console.error(err)
    process.exit(1)
  })
}

function ensureDependencies ({ skipInstall, forceInstall, verbose, env }) {
  if (skipInstall) {
    if (verbose) console.log('[test] skipping dependency install')
    return
  }

  const nodeModules = path.join(testRoot, 'node_modules')
  const needsInstall = forceInstall || !existsSync(nodeModules)
  if (!needsInstall) {
    if (verbose) {
      console.log('[test] dependencies present – skipping npm install')
    }
    return
  }

  console.log('[test] installing dependencies…')
  mkdirSync(nodeModules, { recursive: true })
  const args = ['install', '--no-audit']
  if (!verbose) args.push('--silent')

  const result = spawnSync('npm', args, {
    stdio: 'inherit',
    cwd: testRoot,
    env
  })

  if (result.status) {
    process.exit(result.status)
  }
}

function runFallbackTarget (target, env, verbose) {
  const script = FALLBACK_TARGETS.get(target)
  if (verbose) console.log(`[test] delegating to npm run ${script}`)
  const result = spawnSync('npm', ['run', script], {
    stdio: 'inherit',
    cwd: testRoot,
    env
  })
  return result.status || 0
}

export function syncEnvFile () {
  const preferred = process.env.ORO_ENV_FILENAME || '.oro.env'
  const candidates = [preferred]

  const sourceEntry = candidates
    .map((name) => ({ name, path: path.join(repoRoot, name) }))
    .find((entry) => existsSync(entry.path))

  if (!sourceEntry) return

  const dest = path.join(testRoot, sourceEntry.name)
  if (!existsSync(dest)) {
    copyFileSync(sourceEntry.path, dest)
    return
  }

  try {
    const srcContent = readFileSync(sourceEntry.path)
    const destContent = readFileSync(dest)
    if (!srcContent.equals(destContent)) {
      copyFileSync(sourceEntry.path, dest)
    }
  } catch (err) {
    console.warn(
      `Warning: failed to sync ${sourceEntry.name}:`,
      err?.message || err
    )
  }
}

function collectEnvOverrides ({ match, skip, only }) {
  const overrides = {}
  if (match) overrides.ORO_TEST_GREP = match
  if (skip) overrides.ORO_TEST_SKIP = skip
  if (only) overrides.ORO_TEST_ONLY = only
  return overrides
}

function listTargets () {
  const groups = [
    { label: 'Direct', items: [...DESKTOP_TARGETS, ...RUNTIME_CORE_TARGETS] },
    { label: 'Delegated', items: [...FALLBACK_TARGETS.keys()] }
  ]
  console.log('Available targets:')
  for (const group of groups) {
    console.log(`  ${group.label}:`)
    for (const item of group.items) {
      console.log(`    - ${item}`)
    }
  }
}

function printHelp () {
  console.log(`Usage: node test/scripts/run.js [target] [options]

Targets:
  desktop (default)
  runtime-core
  desktop-autoindex
  desktop-spa
  android
  android-emulator
  ios-simulator
  cli
  cli-ip
  node

Options:
  -t, --target <name>          Explicit test target
      --entry <file>           Desktop entry file (defaults to ./index.js)
      --quick                  Desktop quick mode (reuse staged assets, skip strict pass)
      --strict / --no-strict   Control lifecycle strict pass for desktop
      --reuse-workdir          Reuse staged workdir between runs
      --refresh-workdir        Force staged workdir refresh
      --keep-workdir           Keep staged workdir after completion
      --skip-test-extensions   Skip native test extensions
      --skip-desktop-extension Skip desktop extension build
      --match <regex>          Filter tests whose names match regex (ORO_TEST_GREP)
      --skip <regex>           Skip tests whose names match regex (ORO_TEST_SKIP)
      --only <pattern>         Only run tests matching pattern (ORO_TEST_ONLY)
      --install                Force npm install in test/
      --skip-install           Skip npm install even if node_modules is missing
      --oroc-arg <value>       Extra argument forwarded to oroc (repeatable)
      --verbose                Verbose runner logging
      --list-targets           Print supported targets
  -h, --help                   Show this message`)
}
