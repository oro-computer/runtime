import test from 'node:test'
import assert from 'node:assert/strict'
import { existsSync, mkdtempSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { installSmokePackages } from '../../bin/npm-smoke-install.js'
import { createNpmFixtureEnvironment, NPM_FIXTURE_TIMEOUT } from '../scripts/npm-fixture.js'

const npmCli = process.env.npm_execpath || path.join(path.dirname(process.execPath), process.platform === 'win32'
  ? 'node_modules/npm/bin/npm-cli.js'
  : '../lib/node_modules/npm/bin/npm-cli.js')

test('npm smoke installs the exact offline family and executes verification hooks locally and globally', {
  skip: !existsSync(npmCli) ? 'The npm CLI is required' : false,
  timeout: 3 * NPM_FIXTURE_TIMEOUT + 10000
}, () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro npm lifecycle '))
  const platformPackage = `@oro-computer/runtime-${process.platform}-${process.arch}`
  const version = '0.0.0-fixture'
  const meta = JSON.parse(readFileSync(new URL('../../npm/packages/@oro-computer/runtime/package.json', import.meta.url), 'utf8'))
  meta.version = version
  meta.optionalDependencies = Object.fromEntries(Object.keys(meta.optionalDependencies).map(name => [name, version]))
  assert.ok(meta.optionalDependencies[platformPackage], 'Fixture host must be a supported platform')
  const manifests = [
    { name: platformPackage, version, os: [process.platform], cpu: [process.arch], scripts: { install: 'node verify.cjs' } },
    { name: '@oro-computer/runtime-node', version },
    meta
  ]
  const fixtures = manifests.map((manifest, index) => {
    const fixture = path.join(root, `package ${index}`)
    mkdirSync(fixture)
    writeFileSync(path.join(fixture, 'package.json'), JSON.stringify(manifest))
    return fixture
  })
  writeFileSync(path.join(fixtures[0], 'verify.cjs'), 'require("node:fs").writeFileSync("verified", "platform hook ran"); console.log("platform hook ran")\n')
  mkdirSync(path.join(fixtures[2], 'bin'))
  writeFileSync(path.join(fixtures[2], 'bin/oroc.js'), '#!/usr/bin/env node\n')
  const environment = createNpmFixtureEnvironment(root, {
    ...process.env,
    NPM_CONFIG_IGNORE_SCRIPTS: 'true',
    NPM_CONFIG_USERCONFIG: path.join(root, 'must-not-read.npmrc')
  })
  function npm (args, cwd) {
    const result = spawnSync(process.execPath, [npmCli, ...args], {
      cwd, env: environment, encoding: 'utf8', timeout: NPM_FIXTURE_TIMEOUT
    })
    const details = `npm ${args.join(' ')} in ${cwd}\n` +
      `deadline=${NPM_FIXTURE_TIMEOUT}ms status=${result.status} signal=${result.signal}\n` +
      `${result.error?.stack || ''}\n${result.stdout || ''}${result.stderr || ''}`
    assert.equal(result.error, undefined, details)
    assert.equal(result.status, 0, details)
    return result.stdout
  }
  const packed = JSON.parse(npm(['pack', ...fixtures, '--json', '--foreground-scripts=false', '--pack-destination', root], root))
  assert.equal(packed.length, manifests.length)
  const tarballs = manifests.map(manifest => path.join(root, packed.find(entry => entry.name === manifest.name).filename))

  // Exercise the same installer as release verification with a package whose
  // lifecycle script records whether npm actually executed it.
  for (const globalInstall of [false, true]) {
    const project = path.join(root, globalInstall ? 'global consumer' : 'local consumer')
    const prefix = globalInstall ? path.join(root, 'global prefix') : project
    mkdirSync(project)
    installSmokePackages({
      project,
      prefix,
      tarballs,
      globalInstall,
      npm
    })
    const modules = path.join(prefix, globalInstall && process.platform !== 'win32' ? 'lib/node_modules' : 'node_modules')
    assert.equal(readFileSync(path.join(modules, platformPackage, 'verified'), 'utf8'), 'platform hook ran')
    for (const manifest of manifests) {
      const installed = JSON.parse(readFileSync(path.join(modules, manifest.name, 'package.json'), 'utf8'))
      assert.equal(installed.name, manifest.name)
      assert.equal(installed.version, version)
    }
    for (const name of Object.keys(meta.optionalDependencies)) {
      if (name !== platformPackage) assert.equal(existsSync(path.join(modules, name)), false)
    }
  }
})
