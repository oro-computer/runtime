import test from 'node:test'
import assert from 'node:assert/strict'
import { copyFileSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
const timeout = 30000
const compiler = process.platform === 'linux' && ['clang++-18', 'clang++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore', timeout }).status === 0)

function run (command, args, options = {}) {
  const result = spawnSync(command, args, { encoding: 'utf8', timeout, ...options })
  assert.equal(result.error, undefined, `${command}: ${result.error?.message}`)
  assert.equal(result.status, 0, `${command}: ${result.stderr}`)
  return result.stdout
}

function inspectObject (filename, cpu) {
  const object = readFileSync(filename)
  assert.equal(object.readUInt32LE(0), 0xfeedfacf, 'compiler must produce a 64-bit Mach-O object')
  assert.equal(object.readUInt32LE(4), cpu, 'object must use the requested architecture')
  assert.equal(object.readUInt32LE(12), 1, 'compiler must produce a relocatable object')
  const sections = []
  let symbols
  let offset = 32
  const commandsEnd = offset + object.readUInt32LE(20)
  assert.ok(commandsEnd <= object.length)
  for (let index = 0; index < object.readUInt32LE(16); index++) {
    const command = object.readUInt32LE(offset)
    const size = object.readUInt32LE(offset + 4)
    assert.ok(size >= 8 && offset + size <= commandsEnd)
    if (command === 0x19) { // LC_SEGMENT_64
      const count = object.readUInt32LE(offset + 64)
      assert.ok(72 + count * 80 <= size)
      for (let section = 0; section < count; section++) {
        const start = offset + 72 + section * 80
        sections.push({
          name: object.toString('ascii', start, start + 16).split('\0')[0],
          segment: object.toString('ascii', start + 16, start + 32).split('\0')[0],
          size: object.readBigUInt64LE(start + 40)
        })
      }
    } else if (command === 2) { // LC_SYMTAB
      symbols = {
        offset: object.readUInt32LE(offset + 8),
        count: object.readUInt32LE(offset + 12),
        strings: object.readUInt32LE(offset + 16),
        stringsSize: object.readUInt32LE(offset + 20)
      }
    }
    offset += size
  }
  assert.equal(offset, commandsEnd)
  assert.ok(symbols, 'object must retain its symbol table')
  assert.ok(symbols.offset + symbols.count * 16 <= object.length)
  assert.ok(symbols.strings + symbols.stringsSize <= object.length)
  let defined = false
  for (let index = 0; index < symbols.count; index++) {
    const start = symbols.offset + index * 16
    const nameOffset = object.readUInt32LE(start)
    assert.ok(nameOffset < symbols.stringsSize)
    const nameStart = symbols.strings + nameOffset
    const nameEnd = object.indexOf(0, nameStart)
    assert.ok(nameEnd >= nameStart && nameEnd < symbols.strings + symbols.stringsSize)
    if (object.toString('utf8', nameStart, nameEnd) !== '_oro_apple_bitcode_probe') continue
    assert.equal(object[start + 4] & 0x0f, 0x0f, 'probe must remain an externally defined native symbol')
    const section = sections[object[start + 5] - 1]
    assert.equal(section?.segment, '__TEXT')
    assert.equal(section?.name, '__text')
    assert.ok(section.size > 0n, 'probe must contain native machine code')
    defined = true
  }
  assert.ok(defined, 'object must export the probe function')
  return sections
}

const targets = [
  { name: 'iOS arm64 device', arch: 'arm64', device: true, triple: 'arm64-apple-ios15.0', cpu: 0x0100000c },
  { name: 'iOS arm64 simulator', arch: 'arm64', device: false, triple: 'arm64-apple-ios15.0-simulator', cpu: 0x0100000c },
  { name: 'iOS x64 simulator', arch: 'x86_64', device: false, triple: 'x86_64-apple-ios15.0-simulator', cpu: 0x01000007 }
]

for (const target of targets) {
  test(`${target.name} runtime flags produce native code without embedded bitcode`, {
    skip: process.platform !== 'linux'
      ? 'No-SDK cross-compilation is qualified on Linux'
      : !compiler ? 'Clang is required for the iOS cross-compilation check' : false
  }, () => {
    const directory = mkdtempSync(path.join(tmpdir(), 'oro-apple-bitcode-'))
    mkdirSync(path.join(directory, 'bin'))
    const script = path.join(directory, 'bin', 'cflags.sh')
    copyFileSync(path.join(root, 'bin', 'cflags.sh'), script)
    const source = path.join(directory, 'probe.cc')
    writeFileSync(source, 'extern "C" int oro_apple_bitcode_probe (int value) { return value * 3; }\n')
    // No headers or platform libraries are needed to inspect the compiler's object output.
    const flags = run('bash', ['-c', `
      xcrun () { printf '%s\\n' "$ORO_TEST_SDK"; }
      export -f xcrun
      bash "$1"
    `, 'apple-flags', script], {
      env: {
        ...process.env,
        TARGET_HOST: 'Darwin',
        ARCH: target.arch,
        TARGET_OS_IPHONE: target.device ? '1' : '0',
        TARGET_IPHONE_SIMULATOR: target.device ? '0' : '1',
        TARGET_OS_ANDROID: '0',
        TARGET_ANDROID_EMULATOR: '0',
        IPHONEOS_VERSION_MIN: '15.0',
        IOS_SIMULATOR_VERSION_MIN: '15.0',
        ORO_EXCLUDE_BUILD_METADATA: '1',
        ORO_TEST_SDK: directory,
        CXX: compiler,
        CFLAGS: '',
        CXXFLAGS: '',
        DEBUG: '',
        ORO_CI_FAST_COMPILE: ''
      }
    }).trim().split(/\s+/)
    function compile (name, extraFlags) {
      const filename = path.join(directory, `${name}.o`)
      run(compiler, [...flags, `--target=${target.triple}`, ...extraFlags, '-c', source, '-o', filename])
      return inspectObject(filename, target.cpu)
    }
    const control = compile('with-bitcode', ['-fembed-bitcode'])
    assert.ok(control.some(section => section.segment === '__LLVM' && section.name === '__bitcode' && section.size > 0n),
      'positive control must reproduce the embedded bitcode that inflated the npm package')
    const actual = compile('runtime', [])
    assert.ok(actual.every(section => section.segment !== '__LLVM'), 'runtime flags must omit embedded LLVM bitcode')
  })
}
