import test from 'node:test'
import assert from 'node:assert/strict'
import { existsSync, mkdtempSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { installSmokePackages } from '../../bin/npm-smoke-install.js'

const npmCli = process.env.npm_execpath || path.join(path.dirname(process.execPath), process.platform === 'win32'
  ? 'node_modules/npm/bin/npm-cli.js'
  : '../lib/node_modules/npm/bin/npm-cli.js')

test('npm smoke installs execute the platform verification hook locally and globally', {
  skip: !existsSync(npmCli) ? 'The npm CLI is required' : false,
  timeout: 60000
}, () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro npm lifecycle '))
  const platformPackage = `@oro-computer/runtime-${process.platform}-${process.arch}`
  const version = '0.2.0'
  const fixture = path.join(root, 'platform')
  mkdirSync(fixture)
  writeFileSync(path.join(fixture, 'package.json'), JSON.stringify({
    name: platformPackage,
    version,
    scripts: { install: 'node verify.cjs' }
  }))
  writeFileSync(path.join(fixture, 'verify.cjs'), 'require("node:fs").writeFileSync("verified", "platform hook ran"); console.log("platform hook ran")\n')
  const environment = {
    ...process.env,
    npm_config_cache: path.join(root, 'cache'),
    npm_config_userconfig: path.join(root, 'empty.npmrc')
  }
  writeFileSync(environment.npm_config_userconfig, '')
  const packed = spawnSync(process.execPath, [npmCli, 'pack', '--json', '--pack-destination', root], {
    cwd: fixture, env: environment, encoding: 'utf8', timeout: 20000
  })
  assert.equal(packed.status, 0, packed.stderr)
  const tarballs = [path.join(root, JSON.parse(packed.stdout)[0].filename)]

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
      npm (args, cwd) {
        const result = spawnSync(process.execPath, [npmCli, ...args], { cwd, env: environment, encoding: 'utf8', timeout: 20000 })
        assert.equal(result.status, 0, result.error?.message || result.stdout + result.stderr)
      }
    })
    const modules = path.join(prefix, globalInstall && process.platform !== 'win32' ? 'lib/node_modules' : 'node_modules')
    assert.equal(readFileSync(path.join(modules, platformPackage, 'verified'), 'utf8'), 'platform hook ran')
  }
})
