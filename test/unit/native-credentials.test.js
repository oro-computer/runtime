import test from 'node:test'
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const available = process.platform === 'linux' && spawnSync('pkg-config', ['--exists', 'gtk+-3.0']).status === 0 &&
  ['Xvfb', 'gnome-keyring-daemon', 'dbus-run-session'].every(program => spawnSync('which', [program]).status === 0)
test('Linux password chooser uses an isolated real Secret Service and preserves filtering, cancellation and retry', {
  skip: process.platform !== 'linux' || (!available && !process.env.ORO_REQUIRE_NATIVE_PARITY_TESTS), timeout: 120000
}, () => {
  assert.ok(available, 'Requires GTK 3 development files, Xvfb, GNOME keyring and dbus-run-session')
  const result = spawnSync(process.execPath, [fileURLToPath(new URL('../scripts/test-native-credentials.js', import.meta.url))], {
    encoding: 'utf8', timeout: 110000, env: process.env
  })
  assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout || ''}${result.stderr || ''}`)
  assert.match(result.stdout, /linux-password-selection-filter-cancel-lifecycle-retry-passed/)
})
