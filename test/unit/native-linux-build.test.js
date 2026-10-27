import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
test('Linux native packaging rejects source escapes and generates linkable initializers and valid user units', {
  skip: process.platform !== 'linux', timeout: 60000
}, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-native-linux-build-'))
  const compiler = process.env.ORO_TEST_CXX || 'clang++'
  function run (program, args) {
    const result = spawnSync(program, args, {
      encoding: 'utf8', timeout: 30000, env: { ...process.env, XDG_RUNTIME_DIR: directory }
    })
    assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout || ''}${result.stderr || ''}`)
    return result.stdout
  }
  const fixture = path.join(directory, 'fixture')
  run(compiler, ['-std=c++20', '-Wall', '-Werror', '-I' + root, path.join(root, 'test/fixtures/native-records/NativeLinuxBuild.cc'), '-o', fixture])
  assert.match(run(fixture, [directory]), /linux-native-source-boundaries-initializers-and-service-generation-passed/)
  const entry = path.join(directory, 'entry.cc')
  fs.writeFileSync(entry, 'extern "C" void oro_runtime_native_initialize(void*); extern int public_initializers; int main () { oro_runtime_native_initialize(nullptr); return public_initializers == 1 ? 0 : 1; }\n')
  const binary = path.join(directory, 'compiled-initializers')
  run(compiler, ['-std=c++20', entry, path.join(directory, 'project/native/startup.cc'), path.join(directory, 'output/native-initializers.cc'), '-o', binary])
  run(binary, [])
  run('systemd-analyze', ['verify', '--user', path.join(directory, 'package/usr/lib/systemd/user/org.oro.PublicNativeLinux-native.service')])
  const activation = fs.readFileSync(path.join(directory, 'package/usr/share/dbus-1/services/org.oro.PublicNativeLinux.NativePush.service'), 'utf8')
  assert.match(activation, /Name=org\.oro\.PublicNativeLinux\.NativePush/)
  assert.match(activation, /SystemdService=org\.oro\.PublicNativeLinux-native\.service/)
})
