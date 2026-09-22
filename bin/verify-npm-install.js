#!/usr/bin/env node

import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const [version, commit, ...archives] = process.argv.slice(2)
assert(version && /^[0-9a-f]{40}$/.test(commit || '') && archives.length === 3,
  'Usage: npm run release:verify-npm -- <version> <commit> <platform.tgz> <node.tgz> <runtime.tgz>')
const npmCli = process.env.npm_execpath
assert(npmCli && fs.existsSync(npmCli), 'Run this verifier through npm run release:verify-npm')
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const platformPackage = `@oro-computer/runtime-${process.platform}-${process.arch}`
const expectedVersion = `${version} (${commit.slice(0, 8)})`
const smokeRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'oro npm install '))
const environment = { ...process.env }
console.log(`Smoke installations retained for inspection: ${smokeRoot}`)

function isWithin (parent, filename) {
  const relative = path.relative(parent, filename)
  return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`))
}

// A source checkout or an existing installation must not supply packaged assets.
for (const key of Object.keys(environment)) {
  if (/^(ORO_|PREFIX$|NODE_PATH$|NODE_OPTIONS$|DEBUG$|VERBOSE$|npm_config_|npm_package_)/i.test(key)) {
    delete environment[key]
  }
  if (key.toLowerCase() === 'path') {
    environment[key] = environment[key].split(path.delimiter)
      .filter(entry => entry && !isWithin(root, path.resolve(entry)))
      .join(path.delimiter)
  }
}
environment.ORO_SKIP_PATH_PROMPT = '1'

function requireFile (filename) {
  assert(fs.statSync(filename).isFile() && fs.statSync(filename).size > 0, `Missing or empty file: ${filename}`)
}

const tarballs = archives.map(filename => path.resolve(filename))
for (const filename of tarballs) requireFile(filename)

function run (command, args, cwd, env = environment) {
  console.log(`Checking ${path.basename(command)} ${args.join(' ')} in ${cwd}`)
  const result = spawnSync(command, args, {
    cwd,
    env,
    encoding: 'utf8',
    timeout: 15 * 60 * 1000,
    maxBuffer: 32 * 1024 * 1024
  })
  if (result.stdout) process.stdout.write(result.stdout)
  if (result.stderr) process.stderr.write(result.stderr)
  assert.ifError(result.error)
  assert.equal(result.status, 0, `Command failed (${result.signal || result.status}): ${command} ${args.join(' ')}`)
  return result.stdout.trim()
}

function npm (args, cwd, env) {
  return run(process.execPath, [npmCli, ...args], cwd, env)
}

function verifyTargetLibraries (installationRoot) {
  const nativeArch = process.arch === 'x64' ? 'x86_64' : process.arch
  const targets = [`${nativeArch}-desktop`]
  if (process.platform === 'linux' && process.arch === 'x64') {
    targets.push('arm64-v8a-android', 'x86_64-android')
  }
  if (process.platform === 'darwin') {
    targets.push('arm64-iPhoneOS', 'x86_64-iPhoneSimulator')
    if (process.arch === 'arm64') targets.push('arm64-iPhoneSimulator')
  }
  const actualTargets = fs.readdirSync(path.join(installationRoot, 'lib'), { withFileTypes: true })
    .filter(entry => entry.isDirectory() && /-(desktop|android|iPhoneOS|iPhoneSimulator|ios|ios-simulator)$/.test(entry.name))
    .map(entry => entry.name).sort()
  assert.deepEqual(actualTargets, targets.slice().sort(), 'Packaged target families must match the advertised platform')
  for (const target of targets) {
    requireFile(path.join(installationRoot, 'lib', target, 'liboro-runtime.a'))
  }
  requireFile(path.join(installationRoot, 'objects', `${nativeArch}-desktop`, 'desktop', 'main.o'))
  requireFile(path.join(installationRoot, 'src', 'init.cc'))
  requireFile(path.join(installationRoot, 'include', 'oro', 'platform.h'))
}

for (const globalInstall of [false, true]) {
  const mode = globalInstall ? 'global' : 'local'
  const project = path.join(smokeRoot, `${mode} consumer project`)
  const prefix = globalInstall ? path.join(smokeRoot, 'global prefix') : project
  fs.mkdirSync(project, { recursive: true })
  const scripts = {
    cli: 'oroc',
    build: 'oroc build --prod',
    resources: 'oroc print-build-dir --prod'
  }
  fs.writeFileSync(path.join(project, 'package.json'), JSON.stringify({ private: true, scripts }))
  npm([
    'install', ...(globalInstall ? ['--global'] : []), '--prefix', prefix,
    '--no-package-lock', '--no-save', '--no-audit', '--no-fund', '--ignore-scripts=false', ...tarballs
  ], project)

  const modulesRoot = path.join(prefix, globalInstall && process.platform !== 'win32' ? 'lib/node_modules' : 'node_modules')
  const metaRoot = path.join(modulesRoot, '@oro-computer/runtime')
  const installationRoot = path.join(modulesRoot, platformPackage)
  for (const name of [platformPackage, '@oro-computer/runtime-node', '@oro-computer/runtime']) {
    const manifest = JSON.parse(fs.readFileSync(path.join(modulesRoot, name, 'package.json'), 'utf8'))
    assert.equal(manifest.name, name)
    assert.equal(manifest.version, version)
  }
  const { load } = await import(pathToFileURL(path.join(metaRoot, 'bin/oroc.js')).href)
  const installation = await load()
  assert.equal(fs.realpathSync(installation.env.ORO_HOME), fs.realpathSync(installationRoot))
  assert.equal(installation.platform, process.platform)
  assert.equal(installation.arch, process.arch)
  for (const filename of Object.values(installation.bin)) {
    requireFile(filename)
    assert(isWithin(fs.realpathSync(installationRoot), fs.realpathSync(filename)), `Binary escapes installation: ${filename}`)
  }
  verifyTargetLibraries(installationRoot)
  requireFile(path.join(metaRoot, 'process.js'))
  requireFile(path.join(metaRoot, 'index.d.ts'))

  const binRoot = globalInstall
    ? (process.platform === 'win32' ? prefix : path.join(prefix, 'bin'))
    : path.join(modulesRoot, '.bin')
  requireFile(path.join(binRoot, process.platform === 'win32' ? 'oroc.cmd' : 'oroc'))
  const env = { ...environment }
  const pathKey = Object.keys(env).find(key => key.toLowerCase() === 'path') || 'PATH'
  env[pathKey] = `${binRoot}${path.delimiter}${env[pathKey] || ''}`

  // npm run invokes the installed Unix symlink or Windows command shim.
  assert.equal(npm(['run', '--silent', 'cli', '--', '--version'], project, env), expectedVersion)
  assert.match(npm(['run', '--silent', 'cli', '--', '--help'], project, env), /usage/i)
  const reportedPrefix = npm(['run', '--silent', 'cli', '--', '--prefix'], project, env)
  assert.equal(fs.realpathSync(reportedPrefix), fs.realpathSync(installationRoot))

  run(process.execPath, ['--input-type=module', '-e', `
    import { createRequire } from 'node:module'
    import { pathToFileURL } from 'node:url'
    import path from 'node:path'
    const require = createRequire(path.join(process.argv[1], 'package.json'))
    try {
      require('@oro-computer/runtime-node')
      const entry = path.join(path.dirname(require.resolve('@oro-computer/runtime-node')), 'index.js')
      await import(pathToFileURL(entry).href)
      process.exit(0)
    } catch (error) {
      console.error(error)
      process.exit(1)
    }
  `, metaRoot], project, env)

  fs.writeFileSync(path.join(project, 'oro.toml'), `
[build]
name = "npm-package-smoke"
copy = "index.html"
output = "dist"
[meta]
version = "1.0.0"
bundle_identifier = "computer.oro.npmsmoke"
`)
  fs.writeFileSync(path.join(project, 'index.html'), '<!doctype html><title>npm smoke</title><script type="module">import process from "oro:process"; console.log(process.platform)</script>')
  npm(['run', '--silent', 'build'], project, env)
  const resources = npm(['run', '--silent', 'resources'], project, env)
  assert(isWithin(path.join(project, 'dist'), resources), `Build output escapes consumer project: ${resources}`)
  requireFile(path.join(resources, 'index.html'))
  assert.deepEqual(fs.readFileSync(path.join(resources, 'oro/process.js')), fs.readFileSync(path.join(metaRoot, 'process.js')))
  const executable = process.platform === 'darwin'
    ? path.join(resources, '..', 'MacOS', 'npm-package-smoke')
    : path.join(resources, `npm-package-smoke${process.platform === 'win32' ? '.exe' : ''}`)
  requireFile(executable)
  console.log(`ok - ${mode} ${platformPackage}@${version}: installed command, paths, adapters, and application build`)
}
