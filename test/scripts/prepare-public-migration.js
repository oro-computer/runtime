import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { createHash, X509Certificate } from 'node:crypto'
import { execFileSync } from 'node:child_process'

const fixture = fileURLToPath(new URL('../fixtures/migration/', import.meta.url))
const instrumentation = process.argv[2] === '--instrumentation'
const state = instrumentation ? null : fs.mkdtempSync(path.join(os.tmpdir(), 'oro-public-migration-'))
const project = instrumentation ? path.resolve(process.argv[3]) : path.join(state, 'project')
const bundle = 'oro.runtime.qualification.migration'
if (instrumentation) {
  assert.equal(JSON.parse(fs.readFileSync(path.join(project, 'public-fixture.json'), 'utf8')).bundle, bundle)
  const android = path.join(project, '.cache/out/android')
  assert.ok(fs.existsSync(path.join(android, 'app/build.gradle')), 'Build the public project before preparing its test APK')
  const target = path.join(android, 'app/src/androidTest/java/public/oro/migration')
  fs.mkdirSync(target, { recursive: true })
  fs.copyFileSync(path.join(fixture, 'SeedInstrumentation.kt'), path.join(target, 'SeedInstrumentation.kt'))
  const init = path.join(project, 'public-instrumentation.gradle')
  fs.writeFileSync(init, "gradle.beforeProject { p -> p.plugins.withId('com.android.application') { p.androidComponents.finalizeDsl { dsl -> dsl.defaultConfig.testInstrumentationRunner = 'public.oro.migration.SeedInstrumentation' } } }\n")
  console.log(JSON.stringify({ cwd: android, command: ['./gradlew', '-I', init, ':app:assembleDevDebugAndroidTest', '--no-configuration-cache'] }, null, 2))
} else {
  fs.chmodSync(state, 0o700)
  fs.cpSync(path.join(fixture, 'public'), project, { recursive: true, force: false, errorOnExist: true, filter: source => path.basename(source) !== '.cache' })
  fs.chmodSync(project, 0o700)
  const key = path.join(state, 'public-key.pem')
  const certificate = path.join(state, 'public-cert.pem')
  execFileSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2', '-subj', '/CN=Oro public migration fixture', '-addext', 'subjectAltName=IP:127.0.0.1', '-keyout', key, '-out', certificate], { stdio: 'ignore' })
  fs.chmodSync(key, 0o600)
  const pin = createHash('sha256').update(new X509Certificate(fs.readFileSync(certificate)).raw).digest('base64')
  const settings = path.join(project, 'oro.toml')
  fs.appendFileSync(settings, '\ntls_pins = "127.0.0.1:45132 sha256/' + pin + '"\n')
  const workflows = path.join(project, 'workflows.json')
  const definitions = JSON.parse(fs.readFileSync(workflows, 'utf8'))
  definitions.workflows[0].request.url = 'https://127.0.0.1:45132/public/alerts'
  fs.writeFileSync(workflows, JSON.stringify(definitions, null, 2) + '\n')
  fs.writeFileSync(path.join(project, 'public-fixture.json'), JSON.stringify({ bundle }) + '\n')
  console.log(JSON.stringify({ project, bundle, certificate, leafCertificateSha256Pin: pin, server: { command: ['node', fileURLToPath(new URL('../fixtures/workflows/public-server.cjs', import.meta.url))], env: { ORO_PUBLIC_WORKFLOW_STATE: state, ORO_PUBLIC_WORKFLOW_PORT: '45132' } } }, null, 2))
}
