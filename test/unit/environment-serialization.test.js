import test from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

import { flattenIni } from '../src/util/ini.js'

const repoRoot = fileURLToPath(new URL('../../', import.meta.url))

test('Windows compiler paths survive shell and INI environment loading', () => {
  const compiler = String.raw`C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin\clang++.exe`
  const result = spawnSync('bash', ['-c', `
    source bin/functions.sh
    host_os () { printf '%s\\n' Win32; }
    native_path () { printf '%s\\n' "$1"; }
    build_env_data
  `], {
    cwd: repoRoot,
    env: {
      ...process.env,
      CXX: compiler,
      ANDROID_HOME: '',
      JAVA_HOME: '',
      ANDROID_SDK_MANAGER: '',
      GRADLE_HOME: '',
      ANDROID_SDK_MANAGER_ACCEPT_LICENSES: ''
    },
    encoding: 'utf8'
  })

  assert.equal(result.status, 0, result.stderr)
  assert.equal(flattenIni(result.stdout).CXX, compiler)

  const loaded = spawnSync('bash', ['-e', '-s'], {
    input: `${result.stdout}\nprintf '%s' "$CXX"\n`,
    env: { ...process.env, CXX: '' },
    encoding: 'utf8'
  })

  assert.equal(loaded.status, 0, loaded.stderr)
  assert.equal(loaded.stdout, compiler)
})
