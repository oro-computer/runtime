import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

const binary = process.env.ORO_BIN
const sdk = process.env.ORO_HOME
const available = process.platform === 'linux' && binary && sdk &&
  fs.existsSync(path.join(sdk, 'src/init.cc')) &&
  spawnSync('clang++', ['--version'], { stdio: 'ignore' }).status === 0 &&
  spawnSync('python3', ['--version'], { stdio: 'ignore' }).status === 0

test('desktop extensions isolate objects and configuration with read-only SDK and source inputs', {
  skip: available ? false : 'Requires a built Linux SDK, Clang and Python',
  timeout: 180000
}, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-native-build-'))
  const readonlySdk = path.join(directory, 'read only SDK')
  const readonlySource = path.join(directory, 'external')
  fs.mkdirSync(path.join(readonlySdk, 'src'), { recursive: true })
  fs.mkdirSync(path.join(readonlySource, 'left'), { recursive: true })
  fs.mkdirSync(path.join(readonlySource, 'right'), { recursive: true })
  for (const name of fs.readdirSync(sdk)) {
    if (name !== 'src') fs.symlinkSync(path.join(sdk, name), path.join(readonlySdk, name))
  }
  for (const name of fs.readdirSync(path.join(sdk, 'src'))) {
    if (name === 'init.cc' || /\.o(?:\.inputs)?$/.test(name)) continue
    fs.symlinkSync(path.join(sdk, 'src', name), path.join(readonlySdk, 'src', name))
  }
  fs.copyFileSync(path.join(sdk, 'src/init.cc'), path.join(readonlySdk, 'src/init.cc'))
  fs.chmodSync(path.join(readonlySdk, 'src/init.cc'), 0o444)
  fs.chmodSync(path.join(readonlySdk, 'src'), 0o555)

  const left = path.join(readonlySource, 'left/probe.cpp')
  const right = path.join(readonlySource, 'right/probe.cpp')
  const archive = path.join(readonlySource, 'provided.a')
  fs.writeFileSync(left, `
    extern "C" int qualification_archive();
    extern "C" __attribute__((visibility("default"))) int qualification_value() {
      return qualification_archive();
    }
  `)
  fs.writeFileSync(right, `
    #include <oro/runtime_init.h>
    #include <string>
    extern "C" __attribute__((visibility("default"))) int qualification_contains(const char* marker) {
      const auto bytes = oro_runtime_init_get_user_config_bytes();
      const auto size = oro_runtime_init_get_user_config_bytes_size();
      return std::string(reinterpret_cast<const char*>(bytes), size).find(marker) != std::string::npos;
    }
  `)
  const provider = path.join(directory, 'provided.c')
  const object = path.join(directory, 'provided.o')
  fs.writeFileSync(provider, 'int qualification_archive(void) { return 42; }\n')
  function run (command, args, options = {}) {
    const result = spawnSync(command, args, { encoding: 'utf8', timeout: 60000, ...options })
    assert.equal(result.status, 0, result.stderr || result.stdout || String(result.error))
    return result.stdout
  }
  run('clang', ['-fPIC', '-c', provider, '-o', object])
  run('ar', ['rcs', archive, object])
  const originals = [left, right, archive].map(filename => fs.readFileSync(filename))
  for (const filename of [left, right, archive]) fs.chmodSync(filename, 0o444)
  for (const filename of ['left', 'right', '']) fs.chmodSync(path.join(readonlySource, filename), 0o555)

  const env = {
    ...process.env,
    ORO_HOME: readonlySdk,
    CXX: 'clang++',
    CC: 'clang',
    DEBUG: '',
    VERBOSE: '',
    ORO_SKIP_PATH_PROMPT: '1'
  }
  function library (root) {
    for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
      const filename = path.join(root, entry.name)
      if (entry.isDirectory()) {
        const found = library(filename)
        if (found) return found
      } else if (entry.name === 'fixture.so') return filename
    }
    return null
  }
  function build (name, marker, excluded) {
    const project = path.join(directory, name)
    fs.mkdirSync(project, { recursive: true })
    fs.writeFileSync(path.join(project, 'index.html'), '<!doctype html><title>Public build fixture</title>')
    fs.writeFileSync(path.join(project, 'oro.toml'), `
[build]
name = "native-build-fixture"
copy = "index.html"
output = "out"
[meta]
version = "1.0.0"
bundle_identifier = "oro.runtime.qualification.desktop"
title = "${marker}"
[build.extensions]
fixture = "${left} ${right} ${archive}"
`)
    run(binary, ['build', '--prod'], { cwd: project, env })
    const filename = library(path.join(project, 'out'))
    assert.ok(filename, 'native extension library was built')
    run('python3', ['-c', `
import ctypes,sys
library=ctypes.CDLL(sys.argv[1])
library.qualification_contains.argtypes=[ctypes.c_char_p]
assert library.qualification_value()==42
assert library.qualification_contains(sys.argv[2].encode())==1
assert library.qualification_contains(sys.argv[3].encode())==0
`, filename, marker, excluded])
  }
  build('first', 'public-build-A', 'public-build-B')
  build('second', 'public-build-B', 'public-build-A')
  build('first', 'public-build-C', 'public-build-A')
  assert.equal(fs.existsSync(path.join(readonlySdk, 'src/init.o')), false)
  for (const [index, filename] of [left, right, archive].entries()) {
    assert.deepEqual(fs.readFileSync(filename), originals[index], 'source and supplied archives remain intact')
  }
  assert.deepEqual(fs.readdirSync(path.join(readonlySource, 'left')), ['probe.cpp'])
  assert.deepEqual(fs.readdirSync(path.join(readonlySource, 'right')), ['probe.cpp'])
})
