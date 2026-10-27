import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtemp, mkdir, writeFile } from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import distributor from '../../src/cli/push-distributor.cjs'

const toolchain = ['--android-home=/public/sdk', '--java-home=/public/jdk', '--gradle-home=/public/gradle']

test('distributor build validates explicit device selection and HTTPS configuration', () => {
  const options = distributor.parseOptions([...toolchain, '--server=https://push.example:8787/', '--install', '--device=public-device'])
  assert.equal(options.server, 'https://push.example:8787')
  assert.equal(options.device, 'public-device')
  assert.equal(options.install, true)
  assert.ok(path.isAbsolute(options.output))
  for (const value of ['http://localhost', 'https://user:pass@localhost', 'https://localhost/path', 'https://localhost?x', 'https://localhost/#x', 'https://localhost:0', 'https://0.0.0.0']) {
    assert.throws(() => distributor.parseOptions([...toolchain, `--server=${value}`]))
  }
  for (const args of [['--offline=true'], ['--unknown'], ['--output'], ['--output='], ['--output=a', '--output=b']]) {
    assert.throws(() => distributor.parseOptions([...toolchain, ...args]))
  }
})

test('device arguments remain literal and are not shell commands', () => {
  const options = distributor.parseOptions([...toolchain, '--install', '--device=literal; $HOME $(command)'])
  assert.equal(options.device, 'literal; $HOME $(command)')
})

test('SDK selection requires matching installed metadata and a nonempty Android jar', async () => {
  const sdk = await mkdtemp(path.join(os.tmpdir(), 'oro-push-sdk-'))
  const platforms = path.join(sdk, 'platforms')
  await mkdir(platforms)
  await assert.rejects(distributor.selectPlatform(sdk), /Install an Android SDK platform/)
  for (const [name, actual, bytes] of [
    ['android-33', '33', 'older'], ['android-34', '34', 'public platform'],
    ['android-36', '35', 'wrong version'], ['android-37.0', '37.0', 'newer platform'],
    ['android-38', '38', '']
  ]) {
    const directory = path.join(platforms, name)
    await mkdir(directory)
    await writeFile(path.join(directory, 'source.properties'), `AndroidVersion.ApiLevel=${actual}\n`)
    await writeFile(path.join(directory, 'android.jar'), bytes)
  }
  assert.equal(await distributor.selectPlatform(sdk), 37)
})
