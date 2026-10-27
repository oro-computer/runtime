import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

const script = fs.readFileSync(new URL('../../bin/publish-npm-modules.sh', import.meta.url), 'utf8')
const stage = script.slice(script.indexOf('function package_platform_variant ()'), script.indexOf('\nfunction publish_cli_package ()'))

for (const failedCopy of [false, true]) {
  test(failedCopy ? 'npm staging aborts before packing if a copy fails' : 'npm staging copies a read-only Android target exactly once', { skip: process.platform === 'win32' }, () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro readonly pack '))
    const root = path.join(directory, 'source')
    const sdk = path.join(directory, 'SDK')
    function write (filename, value = 'public fixture') {
      fs.mkdirSync(path.dirname(filename), { recursive: true })
      fs.writeFileSync(filename, value)
    }
    for (const name of ['npm/packages/@oro-computer/runtime-linux-x64/package.json', 'npm/bin/oroc.js', 'npm/src/index.js', 'assets/icon.png', 'LICENSE.txt', 'NOTICE', 'THIRD_PARTY_NOTICES.md', 'README.md', 'docs/BUILD_ENVIRONMENT.md']) write(path.join(root, name))
    for (const name of ['uv/header.h', 'bin/oroc', 'src/init.cc', 'include/public.hh', 'lib/x86_64-android/libpublic.a', 'lib/x86_64-desktop/libpublic.a']) write(path.join(sdk, name))
    fs.chmodSync(path.join(sdk, 'lib/x86_64-android/libpublic.a'), 0o444)
    fs.chmodSync(path.join(sdk, 'lib/x86_64-android'), 0o555)
    const recorded = path.join(directory, 'copy-count')
    const packed = path.join(directory, 'packed')
    const result = spawnSync('bash', ['-c', `
root="$1"; ORO_HOME="$2"; recorded="$3"; packed="$4"; failed="$5"
platform=linux; do_global_link=0; NO_IOS=1
should_stage_target_directory () { return 0; }
validate_platform_target_selection () { return 0; }
_stage_platform_oroc_binary () { return 0; }
_pack_or_link () { touch "$packed"; }
node () { return 0; }
cp () {
  for argument in "$@"; do
    if [[ "$failed" == true && "$argument" == "$ORO_HOME/uv/header.h" ]]; then return 23; fi
    if [[ "$argument" == "$ORO_HOME/lib/x86_64-android" ]]; then
      printf 'copy\\n' >> "$recorded"
      if [[ $(wc -l < "$recorded") -gt 1 ]]; then return 39; fi
    fi
  done
  command cp "$@"
}
${stage}
package_platform_variant @oro-computer runtime x86_64
`, 'readonly-pack', root, sdk, recorded, packed, String(failedCopy)], { encoding: 'utf8', timeout: 15000 })
    assert.equal(result.status, failedCopy ? 23 : 0, result.stdout + result.stderr)
    assert.equal(fs.existsSync(packed), !failedCopy)
    if (!failedCopy) assert.equal(fs.readFileSync(recorded, 'utf8'), 'copy\n')
    assert.equal(fs.statSync(path.join(sdk, 'lib/x86_64-android/libpublic.a')).mode & 0o777, 0o444)
  })
}
