import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

const source = fs.readFileSync(new URL('../../npm/src/index.js', import.meta.url), 'utf8')

function checkConfiguration (scenario) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro user configuration '))
  const root = path.join(directory, 'read only SDK')
  const filename = path.join(root, 'src/index.mjs')
  fs.mkdirSync(path.dirname(filename), { recursive: true })
  fs.writeFileSync(filename, source)
  const env = { ...process.env }
  for (const key of ['ORO_HOME', 'PREFIX', 'ORO_ENV_FILE', 'ORO_ENV_FILENAME', 'ORO_CONFIG_HOME', 'XDG_CONFIG_HOME', 'LOCALAPPDATA']) delete env[key]
  const result = spawnSync(process.execPath, ['--input-type=module', '-e', `
    import assert from 'node:assert/strict'
    import fs from 'node:fs'
    import os from 'node:os'
    import path from 'node:path'
    import childProcess from 'node:child_process'
    import { EventEmitter } from 'node:events'
    import { syncBuiltinESMExports } from 'node:module'
    import { pathToFileURL } from 'node:url'
    const [filename, directory, scenario] = process.argv.slice(1)
    const root = path.dirname(path.dirname(filename))
    os.homedir = () => path.join(directory, 'public home')
    os.platform = () => scenario.startsWith('darwin') ? 'darwin' : scenario.startsWith('win32') ? 'win32' : 'linux'
    let expected = path.join(os.homedir(), '.config/oro/.oro.env')
    if (scenario === 'linux-xdg') {
      process.env.XDG_CONFIG_HOME = path.join(directory, 'xdg config')
      expected = path.join(process.env.XDG_CONFIG_HOME, 'oro/.oro.env')
    } else if (scenario === 'darwin') {
      expected = path.join(os.homedir(), 'Library/Application Support/oro/.oro.env')
    } else if (scenario === 'win32') {
      process.env.LOCALAPPDATA = path.join(directory, 'local appdata')
      expected = path.join(process.env.LOCALAPPDATA, 'oro/.oro.env')
    } else if (scenario === 'win32-fallback') {
      expected = path.join(os.homedir(), 'AppData/Local/oro/.oro.env')
    } else if (scenario === 'explicit-file') {
      process.env.ORO_CONFIG_HOME = path.join(directory, 'ignored directory')
      process.env.ORO_ENV_FILE = path.join(directory, 'explicit config/toolchain.env')
      expected = process.env.ORO_ENV_FILE
    } else if (scenario === 'legacy') {
      fs.writeFileSync(path.join(root, '.oro.env'), 'CXX="public-legacy"\\n')
    } else if (scenario === 'user-over-legacy') {
      fs.writeFileSync(path.join(root, '.oro.env'), 'CXX="public-legacy"\\n')
      fs.mkdirSync(path.dirname(expected), { recursive: true })
      fs.writeFileSync(expected, 'CXX="public-user"\\n')
    }
    fs.chmodSync(root, 0o555)
    let calls = 0
    childProcess.spawn = (command, args, options) => {
      assert.equal(options.env.ORO_ENV_FILE, expected)
      assert.equal(options.cwd, root)
      calls++
      const child = new EventEmitter()
      queueMicrotask(() => {
        if (scenario === 'failure-retry' && calls === 1) {
          fs.writeFileSync(expected, 'CXX="public-incomplete"\\n')
          child.emit('close', 7)
        } else child.emit('close', 0)
      })
      return child
    }
    syncBuiltinESMExports()
    process.argv = [process.execPath, filename, 'build', '--platform', 'android']
    const installation = await import(pathToFileURL(filename).href)
    assert.equal(installation.env.ORO_ENV_FILE, expected)
    if (scenario === 'failure-retry') {
      await assert.rejects(installation.firstTimeExperienceSetup(), /exit code 7/)
      assert.equal(fs.existsSync(expected), false)
    }
    assert.equal(await installation.firstTimeExperienceSetup(), true)
    assert.equal(fs.existsSync(expected), scenario !== 'legacy')
    const before = calls
    assert.equal(await installation.firstTimeExperienceSetup(), true)
    assert.equal(calls, before)
    if (scenario === 'legacy' || scenario === 'user-over-legacy') assert.equal(calls, 0)
    else {
      assert.equal(calls, scenario === 'failure-retry' ? 2 : 1)
      assert.equal(fs.existsSync(path.join(root, '.oro.env')), false)
    }
  `, filename, directory, scenario], { env, encoding: 'utf8', timeout: 15000 })
  assert.ifError(result.error)
  assert.equal(result.status, 0, result.stderr || result.stdout)
}

for (const scenario of ['linux', 'linux-xdg', 'darwin', 'win32', 'win32-fallback', 'explicit-file', 'legacy', 'user-over-legacy', 'failure-retry']) {
  test(`first-run configuration supports ${scenario} without SDK writes`, () => checkConfiguration(scenario))
}

test('shell setup writes and reads the selected external configuration with spaces', () => {
  if (process.platform === 'win32') return
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro external shell config '))
  const filename = path.join(directory, 'user config/toolchain.env')
  const functions = new URL('../../bin/functions.sh', import.meta.url)
  const result = spawnSync('bash', ['-c', `
    source "$1"
    CXX=public-test-compiler
    write_env_data || exit $?
    unset CXX
    read_env_data || exit $?
    [[ "$CXX" == public-test-compiler ]]
  `, 'oro-config-test', functions.pathname], {
    env: { ...process.env, ORO_ENV_FILE: filename }, encoding: 'utf8', timeout: 10000
  })
  assert.ifError(result.error)
  assert.equal(result.status, 0, result.stderr)
  assert.ok(fs.readFileSync(filename, 'utf8').includes('CXX="public-test-compiler"'))
  assert.equal(fs.statSync(filename).mode & 0o777, 0o600)
})

test('shell setup propagates an unwritable user configuration instead of reporting success', {
  skip: process.platform === 'win32' ? 'Requires POSIX directory permissions' : false
}, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro denied config '))
  fs.chmodSync(directory, 0o555)
  const result = spawnSync('bash', [new URL('../../bin/functions.sh', import.meta.url).pathname,
    '--update-env-data', 'CXX=public-test-compiler'], {
    env: { ...process.env, ORO_ENV_FILE: path.join(directory, '.oro.env') },
    encoding: 'utf8',
    timeout: 10000
  })
  assert.ifError(result.error)
  assert.notEqual(result.status, 0)
  assert.equal(fs.existsSync(path.join(directory, '.oro.env')), false)
})
