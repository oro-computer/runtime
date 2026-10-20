import test from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const compiler = ['clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const javaSettings = spawnSync('java', ['-XshowSettings:properties', '-version'], { encoding: 'utf8' })
const javaHome = process.env.JAVA_HOME || javaSettings.stderr?.match(/java.home = (.+)/)?.[1]?.trim() || ''
const javac = path.join(javaHome, 'bin', 'javac')
const jvm = path.join(javaHome, 'lib', 'server', 'libjvm.so')

test('secure storage resolves application classes from native workers and clears failed lookups', {
  skip: process.platform !== 'linux' || !compiler || !existsSync(javac) || !existsSync(jvm)
    ? 'Requires a Linux C++ compiler and a full JDK'
    : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-secure-storage-jni-'))
  const contextClasses = path.join(directory, 'context-classes')
  const managerClasses = path.join(directory, 'manager-classes')
  const missingClasses = path.join(directory, 'missing')
  mkdirSync(missingClasses)
  const context = path.join(directory, 'ProbeContext.java')
  const manager = path.join(directory, 'SecureStorageManager.java')
  writeFileSync(context, `
    import java.net.*;
    import java.io.File;
    public class ProbeContext {
      private final ClassLoader loader;
      public ProbeContext(String path) throws Exception {
        loader = new URLClassLoader(new URL[] {new File(path).toURI().toURL()}, null);
      }
      public ClassLoader getClassLoader() { return loader; }
    }
  `)
  writeFileSync(manager, `
    package oro.runtime.securestorage;
    public class SecureStorageManager {
      public static boolean probe() { return true; }
    }
  `)
  for (const [filename, output] of [[context, contextClasses], [manager, managerClasses]]) {
    const compiled = spawnSync(javac, ['-d', output, filename], { encoding: 'utf8' })
    assert.equal(compiled.status, 0, compiled.stderr)
  }

  const source = readFileSync(new URL('../../src/runtime/core/services/secure_storage.cc', import.meta.url), 'utf8')
  const start = source.indexOf('#if ORO_RUNTIME_PLATFORM_ANDROID\n  namespace {')
  const end = source.indexOf('\n#if ORO_RUNTIME_PLATFORM_APPLE', start)
  assert.ok(start >= 0 && end > start)
  writeFileSync(path.join(directory, 'secure-storage-class-loader.hh'),
    '#define ORO_RUNTIME_PLATFORM_ANDROID 1\n' + source.slice(start, end))
  const executable = path.join(directory, 'class-loader')
  const fixture = fileURLToPath(new URL('../fixtures/android-secure-storage-class-loader.cc', import.meta.url))
  const compiled = spawnSync(compiler, [
    '-std=c++20', '-pthread',
    '-I' + path.join(javaHome, 'include'),
    '-I' + path.join(javaHome, 'include', 'linux'),
    '-I' + directory,
    fixture,
    '-L' + path.dirname(jvm),
    '-Wl,-rpath,' + path.dirname(jvm),
    '-ljvm', '-o', executable
  ], { encoding: 'utf8' })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [contextClasses, managerClasses, missingClasses], {
    encoding: 'utf8', timeout: 30000
  })
  assert.equal(result.status, 0, result.stderr)
})
