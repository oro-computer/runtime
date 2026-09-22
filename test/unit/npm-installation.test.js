import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const source = fs.readFileSync(new URL('../../npm/src/index.js', import.meta.url), 'utf8')

function checkSetup (host, args, expectedTarget) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'oro npm setup '))
  const filename = path.join(root, 'src', 'index.mjs')
  fs.mkdirSync(path.dirname(filename), { recursive: true })
  fs.writeFileSync(filename, source)
  const env = { ...process.env }
  for (const key of Object.keys(env)) {
    if (/^(ORO_HOME|PREFIX|ORO_ENV_FILENAME)$/i.test(key)) delete env[key]
  }
  const result = spawnSync(process.execPath, ['--input-type=module', '-e', `
    import assert from 'node:assert/strict'
    import os from 'node:os'
    import fs from 'node:fs'
    import path from 'node:path'
    import childProcess from 'node:child_process'
    import { EventEmitter } from 'node:events'
    import { syncBuiltinESMExports } from 'node:module'
    import { pathToFileURL } from 'node:url'
    const [filename, host, argsJson, target] = process.argv.slice(1)
    const root = path.dirname(path.dirname(filename))
    const calls = []
    os.platform = () => host
    childProcess.spawn = (command, args, options) => {
      calls.push({ command, args, options })
      const child = new EventEmitter()
      queueMicrotask(() => child.emit('close', 0))
      return child
    }
    syncBuiltinESMExports()
    process.argv = [process.execPath, filename, ...JSON.parse(argsJson)]
    const { firstTimeExperienceSetup } = await import(pathToFileURL(filename).href)
    const shouldRun = await firstTimeExperienceSetup()
    if (!target) {
      assert.equal(calls.length, 0)
      assert.equal(shouldRun, true)
      assert.equal(fs.existsSync(path.join(root, '.oro.env')), false)
    } else {
      assert.equal(calls.length, 1)
      const call = calls[0]
      assert.equal(call.options.cwd, root)
      assert.equal(call.options.env.ORO_HOME, root)
      assert.equal(call.options.env.PREFIX, root)
      assert.equal(call.command, host === 'win32' ? 'powershell.exe' : './bin/functions.sh')
      assert.equal(call.args.at(-1), host === 'win32' ? '-fte:' + target : target)
      assert.equal(shouldRun, !process.argv.includes('setup'))
      assert.equal(fs.existsSync(path.join(root, '.oro.env')), true)
    }
  `, filename, host, JSON.stringify(args), expectedTarget || ''], {
    env,
    encoding: 'utf8',
    timeout: 10000
  })
  assert.ifError(result.error)
  assert.equal(result.status, 0, result.stderr || result.stdout)
}

for (const host of ['linux', 'darwin', 'win32']) {
  test(`npm CLI inspection on ${host} does not invoke dependency setup`, () => {
    for (const args of [['--version'], ['--help'], ['--prefix']]) {
      checkSetup(host, args)
    }
  })

  test(`npm desktop build setup on ${host} uses the installed package and host target`, () => {
    checkSetup(host, ['build', '--prod'], host === 'win32' ? 'windows' : host)
  })
}

test('npm setup preserves explicit mobile targets and Windows all-target setup', () => {
  checkSetup('linux', ['build', '--platform=android-emulator'], 'android')
  checkSetup('darwin', ['build', '--platform', 'ios-simulator'], 'ios-simulator')
  checkSetup('win32', ['setup'], 'all')
})

test('npm platform lifecycle scripts explicitly use Node on every host', () => {
  const packageRoot = fileURLToPath(new URL('../../npm/packages/@oro-computer/', import.meta.url))
  for (const id of ['linux-x64', 'linux-arm64', 'darwin-x64', 'darwin-arm64', 'win32-x64']) {
    const manifest = JSON.parse(fs.readFileSync(path.join(packageRoot, `runtime-${id}`, 'package.json'), 'utf8'))
    const [host, arch] = id.split('-')
    assert.equal(manifest.scripts.install, `node bin/verify-platform.js ${host} ${arch}`)
  }
})
