import test from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { chmodSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'

const compiler = ['clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const source = readFileSync(new URL('../../src/cli/main.cc', import.meta.url), 'utf8')
const nativeBuild = source.match(/ {8}if \(buildJniLibs\) \{[\s\S]*?\n {8}\}/)?.[0]
assert.ok(nativeBuild)

test('standard Android builds run without allow-exec and display NDK failures', {
  skip: process.platform === 'win32' || !compiler ? 'Requires a POSIX C++ compiler' : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-android-native-'))
  const ndk = path.join(directory, 'ndk-build')
  writeFileSync(ndk, `#!/bin/sh
echo "linker diagnostic" >&2
exit "\${NDK_PROBE_STATUS:-0}"
`)
  chmodSync(ndk, 0o755)

  const filename = path.join(directory, 'native-build.cc')
  const executable = path.join(directory, 'native-build')
  writeFileSync(filename, `
    #include <cstdlib>
    #include <filesystem>
    #include <iostream>
    #include <sstream>
    #include <string>
    using String = std::string;
    void logVerbose (const String& text) { std::cout << text << '\\n'; }
    void logError (const String& text) { std::cerr << text << '\\n'; }
    int main (int argc, char** argv) {
      bool buildJniLibs = true, flagDebugMode = false, debugEnv = false, verboseEnv = false;
      std::stringstream ndkBuild, ndkBuildArgs;
      ndkBuild << argv[1];
      auto _main = std::filesystem::path(argv[2]);
      auto app_mk = _main / "jni" / "Application.mk";
      auto jniLibs = _main / "jniLibs";
      String androidNativePlatform = "android-26";
      ${nativeBuild}
    }
  `)
  const built = spawnSync(compiler, ['-std=c++20', filename, '-o', executable], { encoding: 'utf8' })
  assert.equal(built.status, 0, built.stderr)

  const env = { ...process.env }
  delete env.ORO_ALLOW_EXEC
  for (const status of [0, 7]) {
    const result = spawnSync(executable, [ndk, directory], {
      encoding: 'utf8',
      env: { ...env, NDK_PROBE_STATUS: String(status) },
      timeout: 10000
    })
    assert.equal(result.status, status === 0 ? 0 : 1, result.stderr)
    assert.match(result.stderr, /linker diagnostic/)
    assert.doesNotMatch(result.stderr, /allow-exec|external command execution is disabled/)
    if (status !== 0) {
      assert.match(result.stderr, /Android NDK build failed/)
      assert.match(result.stderr, /command: .*ndk-build/)
    }
  }
})
