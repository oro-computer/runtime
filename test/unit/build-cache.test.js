import test, { before } from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { mkdtempSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const compiler = ['clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
let executable

before(() => {
  if (!compiler) return
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-build-cache-'))
  const source = path.join(directory, 'policy.cc')
  executable = path.join(directory, process.platform === 'win32' ? 'policy.exe' : 'policy')
  writeFileSync(source, `
    #include "build-cache.hh"
    #include <iostream>
    int main (int argc, char** argv) {
      std::map<std::string, std::string> settings;
      std::unordered_set<std::string> flags;
      for (int index = 2; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument.starts_with("--")) {
          flags.insert(argument);
        } else {
          const auto position = argument.find('=');
          settings[argument.substr(0, position)] = argument.substr(position + 1);
        }
      }
      try {
        const auto policy = oro::cli::resolveBuildCachePolicy(settings, flags, argv[1]);
        std::cout << policy.enabled << policy.compiler << policy.gradleBuild
          << policy.gradleConfiguration << '\\n' << policy.gradleArguments();
      } catch (const std::invalid_argument& error) {
        std::cerr << error.what();
        return 2;
      }
    }
  `)
  const include = fileURLToPath(new URL('../../src/cli', import.meta.url))
  const result = spawnSync(compiler, ['-std=c++20', '-I', include, source, '-o', executable], { encoding: 'utf8' })
  assert.equal(result.status, 0, result.stderr)
})

const cases = [
  ['defaults stay off', 'android', [], '0000'],
  ['global config opts in', 'android', ['build_cache=true'], '1111'],
  ['global CLI opts in', 'android', ['--cache'], '1111'],
  ['Android config stays scoped', 'linux', ['android_cache=true'], '0000'],
  ['Android CLI stays scoped', 'linux', ['--android-cache'], '0000'],
  ['platform config overrides global config', 'android', ['build_cache=true', 'android_cache=false'], '0000'],
  ['specific config overrides platform config', 'android', ['android_cache=true', 'android_configuration_cache=false'], '1110'],
  ['configuration cache can opt in alone', 'android', ['android_configuration_cache=true'], '0001'],
  ['task cache can opt in alone', 'android', ['--android-build-cache'], '0010'],
  ['CLI disable overrides specific config', 'android', ['android_configuration_cache=true', '--no-cache'], '0000'],
  ['CLI enable overrides specific config', 'android', ['android_configuration_cache=false', '--cache'], '1111'],
  ['specific CLI disable overrides general enable', 'android', ['--cache', '--no-android-configuration-cache'], '1110'],
  ['specific CLI enable overrides general disable', 'android', ['--no-cache', '--android-configuration-cache'], '0001'],
  ['Android CLI overrides global CLI', 'android', ['--cache', '--no-android-cache'], '0000'],
  ['compiler config inherits across platforms', 'linux', ['build_compiler_cache=true'], '0100'],
  ['platform opt-out overrides compiler default', 'linux', ['build_compiler_cache=true', 'linux_cache=false'], '0000'],
  ['specific compiler config overrides platform opt-out', 'linux', ['linux_cache=false', 'linux_compiler_cache=true'], '0100'],
  ['compiler CLI overrides platform CLI', 'android', ['--no-android-cache', '--compiler-cache'], '0100'],
  ['empty optional setting inherits', 'android', ['build_cache=true', 'android_configuration_cache='], '1111'],
  ['emulator inherits Android config', 'android-emulator', ['android_cache=true'], '1111'],
  ['simulator inherits iOS config', 'ios-simulator', ['ios_cache=true'], '1100'],
  ['macOS aliases share config', 'darwin', ['mac_compiler_cache=true'], '0100'],
  ['Windows aliases share config', 'win32', ['win_cache=true'], '1100']
]

for (const [name, platform, cacheArguments, expected] of cases) {
  test(name, { skip: !compiler }, () => {
    const result = spawnSync(executable, [platform, ...cacheArguments], { encoding: 'utf8' })
    assert.equal(result.status, 0, result.stderr)
    const [policy, gradle] = result.stdout.split('\n')
    assert.equal(policy, expected)
    const switches = gradle.trim().split(' ')
    assert.ok(switches.includes(expected[2] === '1' ? '--build-cache' : '--no-build-cache'))
    assert.ok(switches.includes(expected[3] === '1' ? '--configuration-cache' : '--no-configuration-cache'))
    assert.ok(switches.includes('--configuration-cache-problems=fail'))
  })
}

for (const option of ['cache', 'compiler-cache', 'android-cache', 'android-build-cache', 'android-configuration-cache']) {
  test(`conflicting ${option} flags fail`, { skip: !compiler }, () => {
    const result = spawnSync(executable, ['android', `--${option}`, `--no-${option}`], { encoding: 'utf8' })
    assert.equal(result.status, 2)
    assert.match(result.stderr, /Use only one of/)
  })
}

test('invalid cache configuration fails with the key name', { skip: !compiler }, () => {
  const result = spawnSync(executable, ['android', 'android_configuration_cache=yes'], { encoding: 'utf8' })
  assert.equal(result.status, 2)
  assert.match(result.stderr, /android_configuration_cache.*true or false/)
})
