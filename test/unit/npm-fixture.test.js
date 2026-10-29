import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { createNpmFixtureEnvironment } from '../scripts/npm-fixture.js'

test('npm fixtures replace inherited configuration without Windows case collisions', () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro npm environment '))
  const source = {
    PATH: '/fixture/bin',
    SystemRoot: 'C:\\Windows',
    TEMP: directory,
    npm_execpath: '/fixture/npm-cli.js',
    npm_config_cache: '/wrong/lowercase-cache',
    NPM_CONFIG_CACHE: '/wrong/uppercase-cache',
    NpM_CoNfIg_UsErCoNfIg: '/wrong/user.npmrc',
    NPM_CONFIG_GLOBALCONFIG: '/wrong/global.npmrc',
    NPM_CONFIG_OFFLINE: 'false',
    npm_config_ignore_scripts: 'true',
    npm_config_prefix: '/wrong/global-prefix',
    npm_config_script_shell: '/wrong/shell',
    npm_config_registry: 'https://invalid.fixture',
    NPM_PACKAGE_NAME: 'parent-package',
    npm_package_scripts_prepack: 'parent-prepack',
    NODE_OPTIONS: '--require=parent-hook.cjs',
    node_options: '--import=parent-hook.mjs',
    NODE_AUTH_TOKEN: 'fixture-secret',
    NPM_TOKEN: 'fixture-secret',
    npm_auth_token: 'fixture-secret',
    NPM_PASSWORD: 'fixture-secret',
    NPM_USERNAME: 'fixture-user'
  }
  const original = { ...source }
  const environment = createNpmFixtureEnvironment(directory, source)
  assert.deepEqual(source, original, 'Creating a fixture must not mutate its parent environment')
  for (const key of ['PATH', 'SystemRoot', 'TEMP', 'npm_execpath']) assert.equal(environment[key], source[key])
  for (const key of Object.keys(source).filter(key => /^(npm_config_|npm_package_|node_options|node_auth_token|npm_token|npm_auth|npm_password|npm_username)/i.test(key))) {
    if (!key.startsWith('npm_config_')) assert.equal(environment[key], undefined, `Inherited ${key} must be removed`)
  }
  assert.equal(environment.npm_config_prefix, undefined)
  assert.equal(environment.npm_config_registry, undefined)
  assert.equal(environment.npm_config_script_shell, undefined)
  assert.equal(environment.npm_config_offline, 'true')
  assert.equal(environment.npm_config_ignore_scripts, 'false')
  assert.equal(environment.npm_config_audit, 'false')
  assert.equal(environment.npm_config_fund, 'false')
  assert.equal(environment.npm_config_update_notifier, 'false')
  assert.ok(environment.npm_config_cache.startsWith(directory + path.sep))
  assert.equal(new Set(Object.keys(environment).map(key => key.toLowerCase())).size, Object.keys(environment).length)
})

test('npm fixtures use separate empty user and global files without touching inherited settings', () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro npm settings '))
  const inherited = path.join(directory, 'existing.npmrc')
  writeFileSync(inherited, 'ignore-scripts=true\n')
  const environment = createNpmFixtureEnvironment(directory, { npm_config_userconfig: inherited, npm_config_globalconfig: inherited })
  assert.notEqual(environment.npm_config_userconfig, environment.npm_config_globalconfig)
  assert.notEqual(environment.npm_config_userconfig, inherited)
  assert.notEqual(environment.npm_config_globalconfig, inherited)
  assert.equal(readFileSync(environment.npm_config_userconfig, 'utf8'), '')
  assert.equal(readFileSync(environment.npm_config_globalconfig, 'utf8'), '')
  assert.equal(readFileSync(inherited, 'utf8'), 'ignore-scripts=true\n')
  const second = createNpmFixtureEnvironment(directory, {})
  assert.notEqual(second.npm_config_cache, environment.npm_config_cache)
  assert.notEqual(second.npm_config_userconfig, environment.npm_config_userconfig)
})
