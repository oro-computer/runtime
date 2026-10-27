import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import path from 'node:path'
import os from 'node:os'
import vm from 'node:vm'
import { fileURLToPath } from 'node:url'
import { spawnSync } from 'node:child_process'

const root = fileURLToPath(new URL('../../', import.meta.url))
const source = fs.readFileSync(path.join(root, 'api/internal/platform-request.js'), 'utf8')
const body = source.replace(/^import .*$/gm, '').replaceAll('export async function', 'async function').replace('export default platformRequest', '')
function requests (respond) {
  const calls = []
  const context = vm.createContext({
    ipc: { request: async (...args) => { calls.push(args); return respond(...args) } },
    randomBytes: size => Buffer.alloc(size),
    AbortError: class extends Error {}
  })
  return { calls, request: vm.runInContext(`${body}; platformRequest`, context) }
}

for (const platform of ['ios', 'macos', 'linux', 'windows']) {
  test(`Simulated ${platform} support probes distinguish missing implementations and restricted platform APIs without fabricating operations`, async () => {
    const unavailable = Object.assign(new Error('unsupported'), { code: 'ENOSYS' })
    const { calls, request } = requests((route, options) => route === 'runtime.platform'
      ? { err: unavailable }
      : { data: { schema: 1, platform, feature: options.feature, implementation: platform === 'ios' && options.feature === 'foreground.continuous' ? 'restricted' : 'missing', reason: 'public-fixture', backend: '' } })
    for (const [feature, action] of [['authentication', 'status'], ['credentials', 'status'], ['launcher', 'status'], ['push', 'support'], ['deferred', 'support'], ['foreground', 'support'], ['foreground', 'continuous.support'], ['workflows', 'support']]) {
      const result = await request(feature, action, {})
      assert.equal(result.available, false)
      assert.equal(result.platform, platform)
      assert.equal(result.reason, 'public-fixture')
      assert.equal(result.supported, false)
      if (feature === 'foreground') assert.equal(result.canStart, false)
      if (feature === 'push') assert.equal(result.provider, null)
      if (feature === 'workflows') {
        assert.equal(result.workflows.length, 0)
        assert.equal(result.backgroundJavaScript, false)
        assert.equal(result.nativeIntegrationRequired, false)
        assert.equal(result.maximumBindings, 0)
      }
    }
    assert.equal(calls.length, 16)
    for (const [feature, action] of [['authentication', 'seal'], ['foreground', 'start'], ['foreground', 'status'], ['push', 'register'], ['deferred', 'enqueue'], ['credentials', 'selectPassword'], ['workflows', 'bind']]) {
      const before = calls.length
      await assert.rejects(request(feature, action, {}), error => error === unavailable)
      assert.equal(calls.length, before + 1)
    }
  })
}

test('support normalization preserves permission, backend, protocol and cancellation errors', async () => {
  for (const code of ['EACCES', 'EIO', 'EBUSY', 'EINVAL', 'ABORT_ERR']) {
    const error = Object.assign(new Error('public'), { code })
    const { calls, request } = requests(() => { throw error })
    await assert.rejects(request('foreground', 'support', {}), failure => failure === error)
    assert.equal(calls.length, 1)
  }
  const unsupported = Object.assign(new Error('native backend failed'), { code: 'ENOSYS' })
  for (const data of [{}, { schema: 1, feature: 'foreground', implementation: 'implemented' }, { schema: 1, feature: 'other', implementation: 'missing' }]) {
    const { request } = requests(route => route === 'runtime.platform' ? { err: unsupported } : { data })
    await assert.rejects(request('foreground', 'support', {}), error => error === unsupported)
  }
  const controller = new AbortController()
  const { request } = requests(route => {
    if (route === 'runtime.platform') return { err: unsupported }
    controller.abort()
    return { data: { schema: 1, feature: 'foreground', implementation: 'missing' } }
  })
  await assert.rejects(request('foreground', 'support', {}, controller.signal))
})

test('compiled platform inventory separates runtime work from OS restrictions on all target families', () => {
  const compiler = [process.env.ORO_TEST_CXX, 'clang++', 'g++', 'c++'].filter(Boolean).find(command => spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
  assert.ok(compiler, 'A C++ compiler is required for the native support registry')
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-native-support-'))
  const filename = path.join(directory, 'inventory.cc')
  const executable = path.join(directory, process.platform === 'win32' ? 'inventory.exe' : 'inventory')
  fs.writeFileSync(filename, `#include "src/runtime/native_support.hh"
#include <cassert>
#include <set>
#include <iostream>
using namespace oro::runtime::native;
int main() {
  std::set<std::string_view> names;
  for (const auto feature : features) assert(names.insert(feature).second);
  for (const auto platform : {Platform::Android, Platform::IOS, Platform::MacOS, Platform::Linux, Platform::Windows}) {
    assert(platformName(platform) != "unknown");
    for (const auto feature : features) {
      const auto value = describe(platform, feature);
      assert(value.feature == feature);
      if (value.implementation == Implementation::Implemented) assert(!value.backend.empty() && value.reason.empty());
      else assert(value.backend.empty() && !value.reason.empty());
    }
    assert(describe(platform, "secure-storage").implementation == Implementation::Implemented);
    assert(describe(platform, "records").implementation == Implementation::Implemented);
    assert(describe(platform, "coordination").implementation == Implementation::Implemented);
  }
  assert(describe(Platform::IOS, "foreground.continuous").implementation == Implementation::Restricted);
  assert(describe(Platform::Linux, "foreground.continuous").implementation == Implementation::Implemented);
  assert(describe(Platform::MacOS, "authentication").implementation == Implementation::Missing);
  assert(!knownFeature("typo"));
  std::cout << "inventory-parity-passed\\n";
}`)
  const built = spawnSync(compiler, ['-std=c++20', '-Werror', '-I' + root, '-I' + path.join(root, 'include'), filename, '-o', executable], { encoding: 'utf8', timeout: 30000 })
  assert.equal(built.status, 0, built.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
  assert.match(result.stdout, /inventory-parity-passed/)
})
