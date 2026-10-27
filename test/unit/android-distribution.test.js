import test from 'node:test'
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { cliSourceFingerprint, validateDistribution } from '../../bin/check-android-distribution.js'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import androidSdk from '../../src/cli/android-sdk.cjs'

test('SDK helpers use a private subprocess request and preserve compatibility errors', { skip: process.platform === 'win32' }, async () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-sdk-metadata-'))
  const executable = path.join(directory, 'oroc')
  writeFileSync(executable, `#!/usr/bin/env node
if (process.argv.length !== 2 || process.env.ORO_INTERNAL_ANDROID_SDK_METADATA !== '1') process.exit(2)
if (process.env.ORO_LOG_JSON || process.env.ORO_LOG_FILE) process.exit(3)
switch (process.env.SDK_FIXTURE) {
  case 'old': console.log('Oro CLI help'); break
  case 'invalid': console.log('{}'); break
  case 'missing': console.error('Incomplete Android runtime distribution: app.kt'); process.exit(1); break
  default: console.log(JSON.stringify({ schema: 1, sources: [], apis: [] }))
}
`, { mode: 0o755 })
  const env = { ...process.env, ORO_LOG_JSON: '1', ORO_LOG_FILE: 'unused.log' }
  delete env.ORO_INTERNAL_ANDROID_SDK_METADATA
  const original = { ...env }
  assert.deepEqual(await androidSdk.readAndroidSdk(executable, { cwd: directory, env }), { schema: 1, sources: [], apis: [] })
  assert.deepEqual(env, original, 'Private request state must not change the caller environment')
  for (const scenario of ['old', 'invalid']) {
    await assert.rejects(androidSdk.readAndroidSdk(executable, { env: { ...env, SDK_FIXTURE: scenario } }), /Rebuild or reinstall the Oro CLI and SDK together/)
  }
  await assert.rejects(androidSdk.readAndroidSdk(executable, { env: { ...env, SDK_FIXTURE: 'missing' } }), /Incomplete Android runtime distribution: app.kt/)
})

test('installer preserves fingerprint string quotes through verbose, quiet and direct argument commands', t => {
  const compiler = spawnSync('clang++', ['--version'], { encoding: 'utf8' })
  const shell = spawnSync('bash', ['--version'], { encoding: 'utf8' })
  if (compiler.error || shell.error) {
    t.skip('Requires clang++ and bash')
    return
  }
  const repository = fileURLToPath(new URL('../../', import.meta.url))
  const installer = readFileSync(path.join(repository, 'bin/install.sh'), 'utf8')
  const option = installer.indexOf('cflags+=("-DORO_CLI_SOURCE_FINGERPRINT=')
  const start = installer.lastIndexOf('\n  if [[ "$(host_os)" == "Win32" ]]', option)
  const end = installer.indexOf('\n  fi', option)
  assert(start >= 0 && end > option, 'Missing platform-specific fingerprint flags')
  const flags = installer.slice(start, end + '\n  fi'.length)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro cli fingerprint '))
  const source = path.join(directory, 'fingerprint.cc')
  const fingerprint = '6b' + 'a'.repeat(62)
  writeFileSync(source, `#include <string_view>
static_assert(std::string_view(ORO_CLI_SOURCE_FINGERPRINT) == "${fingerprint}");
`)
  for (const host of ['Linux', 'Win32']) {
    for (const verbose of ['', '1']) {
      const script = `source "$1/bin/functions.sh"
fingerprint_host="$2"
host_os () { printf '%s' "$fingerprint_host"; }
VERBOSE="$3"
cli_source_fingerprint=${fingerprint}
cflags=()
${flags}
quiet clang++ -std=c++17 "\${cflags[@]}" -fsyntax-only fingerprint.cc
`
      const result = spawnSync('bash', ['-c', script, 'fingerprint-check', repository, host, verbose], {
        cwd: directory,
        encoding: 'utf8'
      })
      assert.ifError(result.error)
      assert.equal(result.status, 0, `${host} verbose=${Boolean(verbose)}: ${result.stderr}`)
    }
  }
})

test('distribution rejects stale compiled catalogs/templates and mixed packaged sources', () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro-android-distribution-'))
  mkdirSync(path.join(root, 'src/cli'), { recursive: true })
  mkdirSync(path.join(root, 'bin'), { recursive: true })
  for (const name of ['functions.sh', 'android-functions.sh', 'runtime-artifacts.sh']) {
    writeFileSync(path.join(root, 'bin', name), '#!/usr/bin/env bash\n# public setup fixture\n')
  }
  mkdirSync(path.join(root, 'src/runtime/app'), { recursive: true })
  writeFileSync(path.join(root, 'src/cli/main.cc'), `
static Vector<String> androidRuntimeSourceCatalog () {
  return {
    "src/runtime/app/app.kt",
    "src/runtime/app/platform_operations.kt",
  };
}
`)
  const gradle = "\nimplementation 'androidx.webkit:webkit:1.14.0'\n"
  const manifest = '\n<manifest/>\n'
  writeFileSync(path.join(root, 'src/cli/templates.hh'), `
constexpr auto gGradleBuildForSource = R"GROOVY(${gradle})GROOVY";
constexpr auto gAndroidManifest = R"XML(${manifest})XML";
`)
  const hash = value => createHash('sha256').update(value).digest('hex')
  const sources = ['app', 'platform_operations'].map(name => {
    const filename = `src/runtime/app/${name}.kt`
    const value = `public fixture: ${name}`
    writeFileSync(path.join(root, filename), value)
    return { path: filename, sha256: hash(value) }
  })
  const apis = [
    'authentication.js', 'foreground.js', 'foreground/continuous.js', 'launcher.js', 'background/deferred.js', 'credentials.js', 'platform.js', 'push.js', 'window/back.js', 'window/appearance.js',
    'notification.js', 'workflows.js', 'migration.js', 'internal/platform-request.js', 'internal/credentials.js', 'internal/runtime-contract.json'
  ].map(name => {
    const filename = path.join(root, 'api', name)
    mkdirSync(path.dirname(filename), { recursive: true })
    writeFileSync(filename, 'public API fixture')
    return { path: name, sha256: hash('public API fixture') }
  })
  const valid = {
    schema: 1,
    androidBridgeProtocol: 5,
    cliSourceFingerprint: cliSourceFingerprint(root),
    cliSourceFingerprintAlgorithm: 2,
    sources,
    apis,
    gradleTemplateSha256: hash(gradle),
    manifestTemplateSha256: hash(manifest)
  }
  validateDistribution(valid, root)
  const legacy = { ...valid, cliSourceFingerprintAlgorithm: 1, cliSourceFingerprint: cliSourceFingerprint(root, 1) }
  validateDistribution(legacy, root)
  const historical = { ...legacy }
  delete historical.cliSourceFingerprintAlgorithm
  validateDistribution(historical, root)
  const templateFile = path.join(root, 'src/cli/templates.hh')
  const originalTemplate = readFileSync(templateFile, 'utf8')
  writeFileSync(templateFile, originalTemplate.replace(/\n/g, '\r\n'))
  validateDistribution({ ...valid, cliSourceFingerprint: cliSourceFingerprint(root) }, root)
  writeFileSync(templateFile, originalTemplate)
  assert.throws(() => validateDistribution({ ...valid, sources: sources.slice(0, 1) }, root), /source catalog differs/)
  assert.throws(() => validateDistribution({ ...valid, sources: [...sources, sources[0]] }, root), /source catalog differs/)
  assert.throws(() => validateDistribution({ ...valid, gradleTemplateSha256: hash(gradle.replace('1.14.0', '1.9.0')) }, root), /template differs/)
  assert.throws(() => validateDistribution({ ...valid, androidBridgeProtocol: 3 }, root), /bridge protocol differs/)
  assert.throws(() => validateDistribution({ ...valid, apis: apis.slice(1) }, root), /API inventory differs/)
  const internalApi = path.join(root, 'api/internal/platform-request.js')
  writeFileSync(internalApi, 'stale internal API')
  assert.throws(() => validateDistribution(valid, root), /API differs/)
  writeFileSync(internalApi, 'public API fixture')
  writeFileSync(path.join(root, sources[0].path), 'stale packaged source')
  assert.throws(() => validateDistribution(valid, root), /source differs/)
  writeFileSync(path.join(root, sources[0].path), 'public fixture: app')
  const setupHelper = path.join(root, 'bin/android-functions.sh')
  const originalSetup = readFileSync(setupHelper, 'utf8')
  writeFileSync(setupHelper, originalSetup + '\n# stale packaged bootstrap\n')
  assert.throws(() => validateDistribution(valid, root), /source fingerprint differs/)
  validateDistribution(legacy, root)
  writeFileSync(setupHelper, originalSetup)
  const cli = path.join(root, 'src/cli/main.cc')
  writeFileSync(cli, '/* changed CLI logic */\n' + readFileSync(cli, 'utf8'))
  assert.throws(() => validateDistribution(valid, root), /source fingerprint differs/)
  const beforeHelper = cliSourceFingerprint(root)
  writeFileSync(path.join(root, 'src/cli/push-server.cjs'), '// packaged CLI helper\n')
  assert.notEqual(cliSourceFingerprint(root), beforeHelper, 'Packaged executable helpers must affect the CLI fingerprint')
  const beforeDistributor = cliSourceFingerprint(root)
  mkdirSync(path.join(root, 'src/cli/push-distributor'), { recursive: true })
  writeFileSync(path.join(root, 'src/cli/push-distributor/Protocol.kt'), '// packaged Kotlin distributor\n')
  assert.notEqual(cliSourceFingerprint(root), beforeDistributor, 'Nested Kotlin sources must affect the CLI fingerprint')
  const beforeJava = cliSourceFingerprint(root)
  writeFileSync(path.join(root, 'src/cli/push-distributor/PublicCompatibility.java'), '// packaged tool compatibility source\n')
  assert.notEqual(cliSourceFingerprint(root), beforeJava, 'Packaged Java tool sources must affect the CLI fingerprint')
  const shell = spawnSync('bash', [fileURLToPath(new URL('../../bin/cli-source-fingerprint.sh', import.meta.url)), root], { encoding: 'utf8' })
  if (!shell.error) {
    assert.equal(shell.status, 0, shell.stderr)
    assert.equal(shell.stdout.trim(), cliSourceFingerprint(root), 'Installer and package verifier fingerprint algorithms differ')
  }
})
