import test from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const repository = fileURLToPath(new URL('../../', import.meta.url))

function writeExecutable (filename, script) {
  mkdirSync(path.dirname(filename), { recursive: true })
  writeFileSync(filename, '#!/usr/bin/env bash\n' + script, { mode: 0o755 })
}

function runShell (script, directory, args = []) {
  const result = spawnSync('bash', ['-c', script, 'android-toolchain-check', repository, directory, ...args], {
    cwd: directory,
    env: {
      ...process.env,
      ORO_ENV_FILE: path.join(directory, '.oro.env'),
      ORO_INSTALL_SH_LOGFILE: '/dev/null',
      JAVA_HOME: '',
      JAVA_OPTS: '',
      ANDROID_HOME: '',
      ANDROID_SDK_MANAGER: '',
      ANDROID_SDK_MANAGER_JAVA_OPTS: '',
      GRADLE_HOME: '',
      VERBOSE: '',
      DEBUG: ''
    },
    encoding: 'utf8',
    timeout: 10000
  })
  assert.ifError(result.error)
  assert.equal(result.status, 0, result.stderr + result.stdout)
  return result
}

test('copied and linked setup scripts include their runtime artifact helpers', () => {
  const installer = readFileSync(path.join(repository, 'bin/install.sh'), 'utf8')
  const start = installer.indexOf('    mkdir -p "$ORO_HOME/bin"', installer.indexOf('function _install {'))
  const end = installer.indexOf('\n    if [[ "$(uname -s)"', start)
  assert(start >= 0 && end > start, 'Missing desktop setup script staging')
  const staging = installer.slice(start, end)
  for (const mode of ['0', '1']) {
    const directory = mkdtempSync(path.join(tmpdir(), 'oro setup scripts '))
    runShell(`
root="$1"
ORO_HOME="$2/staged runtime"
do_link="$3"
${staging}
source "$ORO_HOME/bin/android-functions.sh" || exit 1
[[ "$(oro_runtime_canonical_lib_prefix)" == liboro-runtime ]] || exit 1
[[ "$canonical_runtime_lib_prefix" == liboro-runtime ]] || exit 1
`, directory, [mode])
  }
})

test('SDK validation accepts working Java and Android CLI launchers and rejects a broken Java launcher', () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro sdk validation '))
  writeExecutable(path.join(directory, 'java sdkmanager'), `
CLASSPATH="$PWD/sdkmanager-classpath.jar"
[[ "$1" == --version && "$JAVA_OPTS" == '-Xmx256m -Doro.sdk=true' ]] || exit 1
printf '22.0\\n'
`)
  writeExecutable(path.join(directory, 'android'), `
[[ "$1" == --version ]] || exit 1
printf 'Android CLI fixture\\n'
`)
  writeExecutable(path.join(directory, 'cli sdkmanager'), 'exec "$(dirname "$0")/android" "$@"\n')
  writeExecutable(path.join(directory, 'broken sdkmanager'), 'CLASSPATH="$PWD/sdkmanager-classpath.jar"\nexit 1\n')
  runShell(`
source "$1/bin/android-functions.sh"
JAVA_OPTS='-Xmx256m'
ANDROID_SDK_MANAGER_JAVA_OPTS='-Doro.sdk=true'
validate_sdkmanager "$2/java sdkmanager" || exit 1
validate_sdkmanager "$2/cli sdkmanager" || exit 1
if validate_sdkmanager "$2/broken sdkmanager"; then exit 1; fi
[[ "$JAVA_OPTS" == '-Xmx256m' ]] || exit 1
`, directory)
})

const discoveryScript = `
source "$1/bin/android-functions.sh"
discovery_fixture="$2"
get_android_default_search_paths () {
  ANDROID_HOME_SEARCH_PATHS=("$discovery_fixture/first sdk" "$discovery_fixture/second sdk")
  ANDROID_SDK_MANAGER_SEARCH_PATHS=(cmdline-tools/23.0/bin cmdline-tools/latest/bin)
  JAVA_HOME_SEARCH_PATHS=("$discovery_fixture/jdk")
  GRADLE_SEARCH_PATHS=("$discovery_fixture/gradle-9.5.0")
}
get_android_paths
`

function createDiscoveryFixture () {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro android discovery '))
  writeExecutable(path.join(directory, 'jdk/bin/javac'), "printf 'javac 17.0.1\\n'\n")
  writeExecutable(path.join(directory, 'gradle-9.5.0/bin/gradle'), 'exit 0\n')
  return directory
}

test('discovery resolves Java before probing SDK managers and keeps the first SDK root and manager together', () => {
  const directory = createDiscoveryFixture()
  writeExecutable(path.join(directory, 'first sdk/cmdline-tools/latest/bin/sdkmanager'), `
[[ "$1" == --version && "$JAVA_HOME" == "$(cd "$(dirname "$0")/../../../../jdk" && pwd)" ]]
`)
  writeExecutable(path.join(directory, 'second sdk/cmdline-tools/23.0/bin/sdkmanager'), 'exit 0\n')
  runShell(discoveryScript + `
[[ "$ANDROID_HOME" == "$2/first sdk" ]] || exit 1
[[ "$ANDROID_SDK_MANAGER" == cmdline-tools/latest/bin/sdkmanager ]] || exit 1
if _android_setup_required; then exit 1; fi
prompt_yn () { printf 'Unexpected reinstall prompt\\n' >&2; return 1; }
android_install_sdk_manager || exit 1
`, directory)
})

test('discovery clears a stale SDK manager when every candidate fails', () => {
  const directory = createDiscoveryFixture()
  writeExecutable(path.join(directory, 'first sdk/cmdline-tools/latest/bin/sdkmanager'), 'exit 1\n')
  runShell(`ANDROID_HOME="$2/first sdk"
ANDROID_SDK_MANAGER=cmdline-tools/latest/bin/sdkmanager
` + discoveryScript + `
[[ -z "$ANDROID_SDK_MANAGER" ]] || exit 1
if ! _android_setup_required; then exit 1; fi
`, directory)
})

test('discovery falls back to a working SDK root when the configured manager fails', () => {
  const directory = createDiscoveryFixture()
  writeExecutable(path.join(directory, 'first sdk/cmdline-tools/latest/bin/sdkmanager'), 'exit 1\n')
  writeExecutable(path.join(directory, 'second sdk/cmdline-tools/23.0/bin/sdkmanager'), 'exit 0\n')
  runShell(discoveryScript + `
[[ "$ANDROID_HOME" == "$2/second sdk" ]] || exit 1
[[ "$ANDROID_SDK_MANAGER" == cmdline-tools/23.0/bin/sdkmanager ]] || exit 1
if _android_setup_required; then exit 1; fi
`, directory)
})

for (const failure of ['install', 'licenses', 'configuration', 'setup', 'none']) {
  test(`Android setup ${failure} result is not cached as success and can be retried`, () => {
    const directory = mkdtempSync(path.join(tmpdir(), 'oro sdk setup '))
    const manager = path.join(directory, 'sdk with spaces/cmdline-tools/latest/bin/sdkmanager')
    writeExecutable(manager, `
case "$1" in
  --version) printf '23.0\\n'; exit 0 ;;
  --licenses)
    printf 'licenses\\n' >> "$PUBLIC_SDK_LOG"
    [[ "$PUBLIC_FAILURE" == licenses ]] && exit 8
    ;;
  *)
    printf 'install\\n' >> "$PUBLIC_SDK_LOG"
    [[ "$PUBLIC_FAILURE" == install ]] && exit 7
    ;;
esac
exit 0
`)
    runShell(`
source "$1/bin/android-functions.sh"
read_env_data () { :; }
get_android_paths () { :; }
write_env_data () { [[ "$PUBLIC_FAILURE" != configuration ]]; }
android_first_time_experience_setup () { [[ "$PUBLIC_FAILURE" != setup ]] || return 9; }
export PUBLIC_FAILURE="$3"
export PUBLIC_SDK_LOG="$2/sdk-log.txt"
ANDROID_HOME="$2/sdk with spaces"
ANDROID_SDK_MANAGER=cmdline-tools/latest/bin/sdkmanager
BUILD_ANDROID=1
JAVA_OPTS=public-original
ANDROID_SDK_MANAGER_JAVA_OPTS=public-override
unset -v android_fte ANDROID_DEPS_ERROR
first=0
android_fte || first=$?
if [[ "$PUBLIC_FAILURE" == none ]]; then
  [[ "$first" == 0 && "$android_fte" == 1 && -z "$ANDROID_DEPS_ERROR" ]] || exit 31
else
  [[ "$first" != 0 && -z "$android_fte" && "$ANDROID_DEPS_ERROR" == 1 ]] || exit 32
  # A second failure must retry instead of returning the earlier false success.
  second=0
  android_fte || second=$?
  [[ "$second" != 0 && -z "$android_fte" ]] || exit 33
fi
[[ "$JAVA_OPTS" == public-original ]] || exit 34
PUBLIC_FAILURE=none
android_fte || exit 35
[[ "$android_fte" == 1 && -z "$ANDROID_DEPS_ERROR" ]] || exit 36
before="$(cat "$PUBLIC_SDK_LOG")"
android_fte || exit 37
[[ "$(cat "$PUBLIC_SDK_LOG")" == "$before" ]] || exit 38
`, directory, [failure])
  })
}

test('complete Android build packages permit offline setup while incomplete packages still fail and retry', {
  skip: process.platform === 'win32' ? 'Requires POSIX SDK tool names' : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro offline sdk '))
  const sdk = path.join(directory, 'sdk with spaces')
  const packages = [
    ['ndk;29.0.14206865', ['ndk-build']],
    ['platform-tools', ['adb']],
    ['platforms;android-37.0', ['android.jar']],
    ['build-tools;36.0.0', ['aapt2', 'zipalign', 'apksigner', 'd8', 'lib/apksigner.jar', 'lib/d8.jar']]
  ]
  for (const [name, files] of packages) {
    const destination = path.join(sdk, ...name.split(';'))
    mkdirSync(destination, { recursive: true })
    writeFileSync(path.join(destination, 'package.xml'), `<localPackage path="${name}"></localPackage>`)
    writeFileSync(path.join(destination, 'source.properties'), 'Pkg.Revision=1\n')
    for (const file of files) {
      mkdirSync(path.dirname(path.join(destination, file)), { recursive: true })
      writeFileSync(path.join(destination, file), 'public tool fixture')
    }
  }
  writeExecutable(path.join(sdk, 'cmdline-tools/latest/bin/sdkmanager'), `
printf '%s\\n' "$@" >> "$PUBLIC_SDK_LOG"
[[ "$1" == --version ]] && { printf '23.0\\n'; exit 0; }
exit 79
`)
  runShell(`
source "$1/bin/android-functions.sh"
read_env_data () { :; }
get_android_paths () { :; }
write_env_data () { :; }
android_first_time_experience_setup () { :; }
export PUBLIC_SDK_LOG="$2/sdk-log.txt"
ANDROID_HOME="$2/sdk with spaces"
ANDROID_SDK_MANAGER=cmdline-tools/latest/bin/sdkmanager
BUILD_ANDROID=1
JAVA_OPTS=public-original
ANDROID_SDK_MANAGER_JAVA_OPTS=public-override
android_fte || exit 1
[[ "$android_fte" == 1 && ! -f "$PUBLIC_SDK_LOG" ]] || exit 2
[[ "$(bash "$1/bin/android-functions.sh" --android-sdk-packages-installed "$ANDROID_HOME")" == 'oro-android-sdk-packages-installed-v1' ]] || exit 11
unset -v android_fte ANDROID_DEPS_ERROR
printf '' > "$ANDROID_HOME/build-tools/36.0.0/aapt2"
if bash "$1/bin/android-functions.sh" --android-sdk-packages-installed "$ANDROID_HOME"; then exit 12; fi
if android_fte; then exit 3; fi
[[ -z "$android_fte" && "$ANDROID_DEPS_ERROR" == 1 ]] || exit 4
[[ "$(cat "$PUBLIC_SDK_LOG")" == $'--version\nbuild-tools;36.0.0' ]] || exit 5
printf 'public tool fixture' > "$ANDROID_HOME/build-tools/36.0.0/aapt2"
printf '<localPackage path="build-tools;35.0.0"></localPackage>' > "$ANDROID_HOME/build-tools/36.0.0/package.xml"
if android_fte; then exit 6; fi
[[ "$(wc -l < "$PUBLIC_SDK_LOG")" == 4 ]] || exit 7
printf '<localPackage path="build-tools;36.0.0"></localPackage>' > "$ANDROID_HOME/build-tools/36.0.0/package.xml"
android_fte || exit 8
[[ "$android_fte" == 1 && -z "$ANDROID_DEPS_ERROR" ]] || exit 9
[[ "$JAVA_OPTS" == public-original && "$(wc -l < "$PUBLIC_SDK_LOG")" == 4 ]] || exit 10
`, directory)
})

test('native build and flags callers propagate failed Android setup before compiling', () => {
  for (const filename of ['bin/cflags.sh', 'bin/build-runtime-library.sh']) {
    const source = readFileSync(path.join(repository, filename), 'utf8')
    const call = source.split('\n').find(line => /^ {2}android_fte(?:\s|$)/.test(line))
    assert.ok(call)
    const result = spawnSync('bash', ['-c', `android_fte () { return 17; }; ${call}\nexit 0`], { encoding: 'utf8' })
    assert.equal(result.status, 17, filename + ': ' + result.stderr)
  }
  const installer = readFileSync(path.join(repository, 'bin/install.sh'), 'utf8')
  const start = installer.indexOf('if [[ -n "$BUILD_ANDROID" ]]; then\n  android_fte ')
  const end = installer.indexOf('\nif [[ -n "$BUILD_ANDROID" ]]; then', start + 1)
  assert.ok(start >= 0 && end > start)
  for (const declined of [false, true]) {
    const result = spawnSync('bash', ['-c', `
BUILD_ANDROID=1
android_fte () { ${declined ? 'unset BUILD_ANDROID; return 2' : 'return 17'}; }
die () { exit "$1"; }
${installer.slice(start, end)}
exit 0
`], { encoding: 'utf8' })
    assert.equal(result.status, declined ? 0 : 17, result.stderr)
  }
})
