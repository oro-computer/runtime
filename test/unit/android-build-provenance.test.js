import test from 'node:test'
import assert from 'node:assert/strict'
import { existsSync, mkdtempSync, mkdirSync, readFileSync, symlinkSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import { fileURLToPath } from 'node:url'
import path from 'node:path'

const clangAvailable = spawnSync('clang++', ['--version'], { stdio: 'ignore' }).status === 0

test('Android theme and USB glyph references reject arbitrary manifest content', { skip: !clangAvailable }, () => {
  const source = readFileSync(new URL('../../src/cli/main.cc', import.meta.url), 'utf8')
  const start = source.indexOf('static bool androidResourceReference (')
  const end = source.indexOf('// This compiled catalog', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-native-resource-reference-'))
  const filename = path.join(directory, 'reference.cc')
  const executable = path.join(directory, 'reference')
  writeFileSync(filename, `
    #include <regex>
    #include <string>
    using String = std::string;
    ${source.slice(start, end)}
    int main(int argc, char** argv) {
      return argc == 3 && androidResourceReference(argv[1], argv[2]) ? 0 : 1;
    }
  `)
  const compiled = spawnSync('clang++', ['-std=c++20', filename, '-o', executable], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  for (const [value, type] of [['@style/Theme.Public', 'style'], ['@drawable/public_usb', 'drawable']]) {
    assert.equal(spawnSync(executable, [value, type]).status, 0, value)
  }
  for (const [value, type] of [['@style/Theme.Public" android:debuggable="true', 'style'],
    ['@style/../private', 'style'], ['@android:style/Theme', 'style'], ['@drawable/Public', 'drawable'],
    ['@drawable/public.usb', 'drawable'], ['@style/' + 'a'.repeat(161), 'style'], ['@drawable/public', 'style']]) {
    assert.equal(spawnSync(executable, [value, type]).status, 1, value)
  }
})

test('Android extension dependencies reject changing selectors and snapshot versions', {
  skip: !clangAvailable
}, () => {
  const source = readFileSync(new URL('../../src/cli/main.cc', import.meta.url), 'utf8')
  const start = source.indexOf('static bool androidFixedMavenDependency (')
  const end = source.indexOf('// This compiled catalog', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-native-dependencies-'))
  const filename = path.join(directory, 'dependencies.cc')
  const executable = path.join(directory, 'dependencies')
  writeFileSync(filename, `
    #include <regex>
    #include <string>
    using String = std::string;
    ${source.slice(start, end)}
    int main(int argc, char** argv) {
      return argc == 2 && androidFixedMavenDependency(argv[1]) ? 0 : 1;
    }
  `)
  const compiled = spawnSync('clang++', ['-std=c++20', filename, '-o', executable], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  for (const coordinate of ['org.example:feature:1.2.3', 'org.example:snapshot-reader:1.2.3', 'org.example:feature:2.0.0-rc.1']) {
    assert.equal(spawnSync(executable, [coordinate]).status, 0, coordinate)
  }
  for (const coordinate of ['org.example:feature:1.2-SNAPSHOT', 'org.example:feature:1.2-sNaPsHoT',
    'org.example:feature:1.+', 'org.example:feature:latest.release', 'org.example:feature:[1,2)',
    "org.example:feature:1.0'", 'org.example:feature']) {
    assert.equal(spawnSync(executable, [coordinate]).status, 1, coordinate)
  }
})

test('Android resource staging copies nested qualifiers and rejects links outside the declared tree', {
  skip: !clangAvailable || process.platform === 'win32'
}, () => {
  const source = readFileSync(new URL('../../src/cli/main.cc', import.meta.url), 'utf8')
  const start = source.indexOf('static void androidCopyResources (')
  const end = source.indexOf('static Vector<String> androidConfigList (', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-native-resources-'))
  const input = path.join(directory, 'project/res')
  const output = path.join(directory, 'generated/res')
  for (const name of ['drawable', 'values-v31']) mkdirSync(path.join(input, name), { recursive: true })
  writeFileSync(path.join(input, 'drawable/public.xml'), '<vector/>')
  writeFileSync(path.join(input, 'values-v31/theme.xml'), '<resources/>')
  const filename = path.join(directory, 'resources.cc')
  const executable = path.join(directory, 'resources')
  writeFileSync(filename, `
    #include <filesystem>
    #include <stdexcept>
    namespace fs = std::filesystem;
    using Path = fs::path;
    ${source.slice(start, end)}
    int main(int argc, char** argv) {
      if (argc != 3) return 2;
      try { androidCopyResources(argv[1], argv[2]); }
      catch (const std::exception&) { return 3; }
    }
  `)
  const compiled = spawnSync('clang++', ['-std=c++20', filename, '-o', executable], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  assert.equal(spawnSync(executable, [input, output]).status, 0)
  assert.equal(readFileSync(path.join(output, 'drawable/public.xml'), 'utf8'), '<vector/>')
  assert.equal(readFileSync(path.join(output, 'values-v31/theme.xml'), 'utf8'), '<resources/>')
  const links = path.join(directory, 'linked/res')
  const outside = path.join(directory, 'outside.xml')
  mkdirSync(links, { recursive: true })
  writeFileSync(outside, 'excluded public fixture')
  symlinkSync(outside, path.join(links, 'escape.xml'))
  const rejectedOutput = path.join(directory, 'rejected')
  assert.equal(spawnSync(executable, [links, rejectedOutput]).status, 3)
  assert.equal(existsSync(path.join(rejectedOutput, 'escape.xml')), false)
})

test('Android native inventory enters linked ABI directories and retains portable paths', {
  skip: !clangAvailable || process.platform === 'win32'
}, () => {
  const source = readFileSync(new URL('../../src/cli/main.cc', import.meta.url), 'utf8')
  const start = source.indexOf('static Vector<Path> androidNativeArchiveInputs (')
  const end = source.indexOf('// Hash the files', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-linked-native-inventory-'))
  const root = path.join(directory, 'installed-lib')
  const target = path.join(directory, 'build-arm64-lib')
  const empty = path.join(directory, 'empty-lib')
  for (const folder of [root, target, empty, path.join(root, 'x86_64-linux'), path.join(target, 'nested')]) {
    mkdirSync(folder, { recursive: true })
  }
  writeFileSync(path.join(target, 'liboro-runtime.a'), 'public archive fixture')
  writeFileSync(path.join(target, 'not-an-archive.txt'), 'public fixture')
  writeFileSync(path.join(target, 'nested', 'libnested.a'), 'excluded nested fixture')
  writeFileSync(path.join(root, 'x86_64-linux', 'libdesktop.a'), 'excluded desktop fixture')
  symlinkSync(target, path.join(root, 'arm64-v8a-android'), 'dir')
  symlinkSync(path.join(target, 'liboro-runtime.a'), path.join(target, 'libdependency.a'))
  symlinkSync(root, path.join(target, 'nested-link'), 'dir')
  const filename = path.join(directory, 'inventory.cc')
  const executable = path.join(directory, 'inventory')
  writeFileSync(filename, `
    #include <algorithm>
    #include <filesystem>
    #include <iostream>
    #include <stdexcept>
    #include <vector>
    namespace fs = std::filesystem;
    using Path = fs::path;
    template<class T> using Vector = std::vector<T>;
    ${source.slice(start, end)}
    int main(int argc, char** argv) {
      if (argc != 2) return 2;
      const Path root(argv[1]);
      try {
        for (const auto& archive : androidNativeArchiveInputs(root)) {
          std::cout << archive.lexically_relative(root).generic_string() << '\\n';
        }
      } catch (const std::exception& error) {
        std::cerr << error.what();
        return 3;
      }
    }
  `)
  const compiled = spawnSync('clang++', ['-std=c++20', filename, '-o', executable], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [root], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
  assert.deepEqual(result.stdout.trim().split('\n'), [
    'arm64-v8a-android/libdependency.a',
    'arm64-v8a-android/liboro-runtime.a'
  ])
  const missing = spawnSync(executable, [empty], { encoding: 'utf8', timeout: 10000 })
  assert.equal(missing.status, 3)
  assert.match(missing.stderr, /requires native archives/)
})

const pythonAvailable = spawnSync('python3', ['--version'], { stdio: 'ignore' }).status === 0

test('release provenance rejects missing, unsafe, duplicate and wrong-ABI archive declarations', {
  skip: !pythonAvailable
}, () => {
  const verifier = new URL('../../bin/verify-android-release.py', import.meta.url)
  const script = `
import copy, runpy, sys
validate = runpy.run_path(sys.argv[1])['native_archive_evidence']
entry = {'path': 'arm64-v8a-android/liboro-runtime.a', 'sha256': 'ab' * 32}
valid = {'nativeArchives': [entry]}
assert validate(valid, {'arm64-v8a'}) == [entry]
invalid = [{}, {'nativeArchives': []}, {'nativeArchives': [None]},
           {'nativeArchives': [entry, entry]}]
for field, value in [('path', '../arm64-v8a-android/liboro-runtime.a'),
                     ('path', '/arm64-v8a-android/liboro-runtime.a'),
                     ('path', 'arm64-v8a-android/liboro-runtime.so'),
                     ('sha256', 'not-a-digest'), ('sha256', None)]:
    candidate = copy.deepcopy(valid)
    candidate['nativeArchives'][0][field] = value
    invalid.append(candidate)
for candidate in invalid:
    try:
        validate(candidate, {'arm64-v8a'})
    except ValueError:
        continue
    raise AssertionError('Invalid provenance accepted')
try:
    validate(valid, {'arm64-v8a', 'x86_64'})
except ValueError as error:
    assert 'x86_64' in str(error)
else:
    raise AssertionError('Missing second ABI provenance accepted')
`
  const result = spawnSync('python3', ['-c', script, fileURLToPath(verifier)], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})
