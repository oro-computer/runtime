import test from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { chmodSync, existsSync, mkdtempSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'

const compiler = ['clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const source = readFileSync(new URL('../../src/cli/main.cc', import.meta.url), 'utf8')
const start = source.indexOf('      // Generate the wrapper without evaluating')
const end = source.indexOf('      copyWritableFile(prefixPath("src/init.cc")', start)
const shellQuote = source.match(/inline String shellQuote \(const String& value\) \{[\s\S]*?\n\}/)?.[0]
assert.ok(start >= 0 && end > start && shellQuote)

test('Android wrapper generation isolates stale plugins and upgrades existing wrappers', {
  skip: process.platform === 'win32' || !compiler ? 'Requires a POSIX C++ compiler' : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-gradle-bootstrap-'))
  writeFileSync(path.join(directory, 'package.json'), '{"type":"commonjs"}\n')
  const output = path.join(directory, "android project's output")
  const gradleHome = path.join(directory, "Gradle installation's home")
  mkdirSync(path.join(gradleHome, 'bin'), { recursive: true })
  mkdirSync(path.join(output, 'gradle', 'wrapper'), { recursive: true })
  const staleBuild = "throw new GradleException('stale Android project')\n"
  writeFileSync(path.join(output, 'build.gradle'), staleBuild)
  writeFileSync(path.join(output, 'gradle', 'wrapper', 'gradle-wrapper.properties'), 'old wrapper')

  const gradle = path.join(gradleHome, 'bin', 'gradle')
  writeFileSync(gradle, `#!/usr/bin/env node
    const assert = require('node:assert/strict')
    const fs = require('node:fs')
    const path = require('node:path')
    const args = process.argv.slice(2)
    assert.equal(args.includes('--offline'), false)
    assert.equal(args.at(-1), 'wrapper')
    const project = args[args.indexOf('--project-dir') + 1]
    assert.equal(fs.readFileSync(path.join(project, 'build.gradle'), 'utf8'), '')
    assert.match(fs.readFileSync(path.join(project, 'settings.gradle'), 'utf8'), /oro-wrapper/)
    if (process.env.GRADLE_PROBE_FAIL === '1') process.exit(42)
    for (const file of ['gradlew', 'gradlew.bat', 'gradle/wrapper/gradle-wrapper.jar', 'gradle/wrapper/gradle-wrapper.properties']) {
      const target = path.join(project, file)
      fs.mkdirSync(path.dirname(target), { recursive: true })
      fs.writeFileSync(target, 'generated ' + file)
      if (file === 'gradlew') fs.chmodSync(target, 0o755)
    }
  `)
  chmodSync(gradle, 0o755)

  const filename = path.join(directory, 'bootstrap.cc')
  const executable = path.join(directory, 'bootstrap')
  writeFileSync(filename, `
    #include <cstdlib>
    #include <filesystem>
    #include <fstream>
    #include <iostream>
    #include <sstream>
    #include <string>
    namespace fs = std::filesystem;
    using String = std::string;
    using StringStream = std::stringstream;
    using Path = fs::path;
    namespace env {
      String get (const String& key) {
        const auto* value = std::getenv(key.c_str());
        return value ? value : "";
      }
    }
    void writeFile (const Path& file, const String& data) { std::ofstream(file) << data; }
    void logVerbose (const String&) {}
    void logError (const String& text) { std::cerr << text; }
    String gradleJdkHint () { return ""; }
    ${shellQuote}
    int main (int argc, char** argv) {
      const auto output = fs::absolute(argv[1]);
      struct { bool win = false; } platform;
      bool debugEnv = false, verboseEnv = false;
      ${source.slice(start, end)}
    }
  `)
  const built = spawnSync(compiler, ['-std=c++20', filename, '-o', executable], { encoding: 'utf8' })
  assert.equal(built.status, 0, built.stderr)

  const env = { ...process.env, GRADLE_HOME: gradleHome, GRADLE_PROBE_FAIL: '1' }
  const failed = spawnSync(executable, [output], { encoding: 'utf8', env, timeout: 10000 })
  assert.equal(failed.status, 1, failed.stderr)
  assert.match(failed.stderr, /failed to generate the Android Gradle wrapper/)
  assert.doesNotMatch(failed.stderr, /licenses/)
  assert.equal(readFileSync(path.join(output, 'gradle', 'wrapper', 'gradle-wrapper.properties'), 'utf8'), 'old wrapper')

  const result = spawnSync(executable, [output], {
    encoding: 'utf8',
    env: { ...env, GRADLE_PROBE_FAIL: '0' },
    timeout: 10000
  })
  assert.equal(result.status, 0, result.stderr)
  assert.equal(readFileSync(path.join(output, 'build.gradle'), 'utf8'), staleBuild)
  assert.equal(existsSync(path.join(output, 'gradlew')), true)
  assert.equal(readFileSync(path.join(output, 'gradle', 'wrapper', 'gradle-wrapper.properties'), 'utf8'), 'generated gradle/wrapper/gradle-wrapper.properties')
})
