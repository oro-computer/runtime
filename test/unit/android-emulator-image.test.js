import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

test('emulator image selection is independent of the build SDK and reuses its own AVD', {
  skip: process.platform === 'win32' ? 'Requires POSIX shell fixtures' : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro emulator image '))
  const scripts = path.join(directory, 'test/scripts')
  const bin = path.join(directory, 'bin')
  const sdk = path.join(directory, 'sdk')
  const avds = path.join(directory, 'avd')
  const logfile = path.join(directory, 'calls.txt')
  mkdirSync(scripts, { recursive: true })
  mkdirSync(bin)
  mkdirSync(sdk)
  const bootstrap = path.join(scripts, 'bootstrap-android-emulator.sh')
  writeFileSync(bootstrap, readFileSync(new URL('../scripts/bootstrap-android-emulator.sh', import.meta.url)))
  const commands = {
    uname: 'echo x86_64',
    sdkmanager: `
      printf 'sdk:%s\\n' "$*" >> "$PUBLIC_CALLS"
      for package in "$@"; do
        case "$package" in
          'system-images;'*)
            image="$ANDROID_HOME/$(printf '%s' "$package" | tr ';' '/')"
            mkdir -p "$image"
            touch "$image/source.properties"
            ;;
        esac
      done
    `,
    avdmanager: `
      if [[ "$1" == list ]]; then
        for file in "$ANDROID_AVD_HOME"/*.ini; do
          [[ -f "$file" ]] && echo "Name: $(basename "$file" .ini)"
        done
        exit 0
      fi
      printf 'avd:%s\\n' "$*" >> "$PUBLIC_CALLS"
      while (( $# )); do
        if [[ "$1" == -n ]]; then name="$2"; break; fi
        shift
      done
      mkdir -p "$ANDROID_AVD_HOME/$name.avd"
      touch "$ANDROID_AVD_HOME/$name.ini"
      printf 'disk.dataPartition.size=2G\\n' > "$ANDROID_AVD_HOME/$name.avd/config.ini"
    `,
    emulator: 'printf "emulator:%s\\n" "$*" >> "$PUBLIC_CALLS"'
  }
  for (const [name, source] of Object.entries(commands)) {
    writeFileSync(path.join(bin, name), `#!/usr/bin/env bash\n${source}\n`, { mode: 0o755 })
  }
  const env = {
    ...process.env,
    PATH: bin + path.delimiter + process.env.PATH,
    ANDROID_HOME: sdk,
    ANDROID_SDK_PLATFORM: '37.0',
    ANDROID_SDK_MANAGER: '',
    ANDROID_USER_HOME: directory,
    ANDROID_AVD_HOME: avds,
    ORO_ANDROID_EMULATOR_PORT: '5564',
    PUBLIC_CALLS: logfile,
    CI: '1',
    EMULATOR_FLAGS: ''
  }
  for (const [override, level] of [['', '36'], ['34', '34'], ['37.0', '37.0']]) {
    env.ORO_ANDROID_EMULATOR_API_LEVEL = override
    const name = `OROAVD_API_${level.replaceAll('.', '_')}_x86_64_data6G`
    writeFileSync(logfile, '')
    for (let attempt = 0; attempt < 2; attempt++) {
      const result = spawnSync('bash', [bootstrap], { cwd: directory, env, encoding: 'utf8', timeout: 10000 })
      assert.ifError(result.error)
      assert.equal(result.status, 0, result.stderr || result.stdout)
    }
    const calls = readFileSync(logfile, 'utf8').trim().split('\n')
    assert.deepEqual(calls.filter(line => line.startsWith('sdk:')), [
      `sdk:system-images;android-${level};google_apis;x86_64`,
      'sdk:--licenses'
    ])
    assert.deepEqual(calls.filter(line => line.startsWith('avd:')), [
      `avd:--clear-cache create avd -n ${name} -k system-images;android-${level};google_apis;x86_64 -d 1 --force`
    ])
    assert.equal(calls.filter(line => line.startsWith(`emulator:@${name} `)).length, 2)
    assert.match(readFileSync(path.join(avds, `${name}.avd/config.ini`), 'utf8'), /^disk\.dataPartition\.size=6G\n$/)
  }
})
