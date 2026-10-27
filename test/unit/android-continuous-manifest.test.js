import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, writeFileSync, readFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

const cli = process.env.ORO_TEST_NATIVE_CLI

test('compiled CLI generates isolated foreground modes and rejects invalid specialUse purposes', {
  skip: cli ? false : 'Requires rebuilt ORO_TEST_NATIVE_CLI and its paired ORO_HOME/ORO_HOME_API',
  timeout: 120000
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-continuous-manifest-'))
  function generate (name, finite, continuous, purpose, icon) {
    const root = path.join(directory, name)
    mkdirSync(path.join(root, 'public'), { recursive: true })
    writeFileSync(path.join(root, 'public/index.html'), '<!doctype html><title>Public manifest fixture</title>')
    const config = path.join(root, 'oro.toml')
    writeFileSync(config, `[build]
name = "public_monitor"
copy = "public"
[meta]
bundle_identifier = "org.example.publicmonitor"
version = "1.0.0"
[permissions]
allow_usb = false
[foreground]
enabled = ${finite}
continuous_enabled = ${continuous}
${purpose === undefined ? '' : `continuous_subtype = ${JSON.stringify(purpose)}`}
${icon === undefined ? '' : `notification_icon = ${JSON.stringify(icon)}`}
`)
    const output = path.join(root, 'editor')
    const result = spawnSync(cli, ['build', '--platform', 'android', '--editor-project', output, '--config', config], {
      cwd: directory,
      env: { ...process.env, ORO_LOG_NO_COLOR: '1' },
      encoding: 'utf8',
      timeout: 30000
    })
    assert.ifError(result.error)
    return { result, manifest: () => readFileSync(path.join(output, 'app/src/main/AndroidManifest.xml'), 'utf8') }
  }
  const monitoring = generate('continuous', false, true, 'Public <monitor> & "Stop"')
  assert.equal(monitoring.result.status, 0, monitoring.result.stderr)
  const xml = monitoring.manifest()
  assert.match(xml, /RuntimeContinuousService/)
  assert.match(xml, /foregroundServiceType="specialUse"/)
  assert.match(xml, /android:exported="false"/)
  assert.match(xml, /PROPERTY_SPECIAL_USE_FGS_SUBTYPE/)
  assert.match(xml, /Public &lt;monitor&gt; &amp; &quot;Stop&quot;/)
  assert.match(xml, /FOREGROUND_SERVICE_SPECIAL_USE/)
  assert.doesNotMatch(xml, /RuntimeForegroundService|FOREGROUND_SERVICE_DATA_SYNC/)
  const finite = generate('finite', true, false)
  assert.equal(finite.result.status, 0, finite.result.stderr)
  assert.match(finite.manifest(), /RuntimeForegroundService/)
  assert.doesNotMatch(finite.manifest(), /RuntimeContinuousService|FOREGROUND_SERVICE_SPECIAL_USE/)
  const both = generate('both', true, true, 'Public monitor')
  assert.equal(both.result.status, 0, both.result.stderr)
  assert.match(both.manifest(), /RuntimeForegroundService/)
  assert.match(both.manifest(), /RuntimeContinuousService/)
  const icon = generate('custom-icon', true, true, 'Public monitoring', '@drawable/public_status')
  assert.equal(icon.result.status, 0, icon.result.stderr)
  assert.match(icon.manifest(), /oro\.runtime\.foreground\.small_icon/)
  assert.match(icon.manifest(), /android:resource="@drawable\/public_status"/)
  for (const value of ['public_status', '@mipmap/public_status', '@drawable/has-dash', '@drawable/../bad']) {
    const invalid = generate('bad-icon-' + value.length + value.charCodeAt(0), true, false, undefined, value)
    assert.notEqual(invalid.result.status, 0)
    assert.match(invalid.result.stdout + invalid.result.stderr, /foreground\.notification_icon must/)
  }
  const boundary = generate('utf8-boundary', false, true, 'é'.repeat(256))
  assert.equal(boundary.result.status, 0, boundary.result.stderr)
  for (const [index, purpose] of [undefined, '', '   ', 'é'.repeat(257), 'Public\nmonitor'].entries()) {
    const rejected = generate(`invalid-${index}`, false, true, purpose)
    assert.notEqual(rejected.result.status, 0)
    assert.match(rejected.result.stdout + rejected.result.stderr, /foreground\.continuous_subtype must describe/)
  }
})
