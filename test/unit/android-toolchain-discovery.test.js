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
