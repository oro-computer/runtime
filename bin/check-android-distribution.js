#!/usr/bin/env node

import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import fs from 'node:fs'
import path from 'node:path'
import androidSdk from '../src/cli/android-sdk.cjs'
import { fileURLToPath } from 'node:url'

const requiredApis = [
  'authentication.js', 'foreground.js', 'foreground/continuous.js', 'launcher.js', 'background/deferred.js', 'credentials.js', 'platform.js', 'push.js', 'window/back.js', 'window/appearance.js',
  'notification.js', 'workflows.js', 'migration.js', 'internal/platform-request.js', 'internal/credentials.js', 'internal/runtime-contract.json'
]

function digest (value) {
  return createHash('sha256').update(value).digest('hex')
}

function digestFile (filename) {
  const hash = createHash('sha256')
  const descriptor = fs.openSync(filename, 'r')
  const buffer = Buffer.alloc(65536)
  try {
    let count
    while ((count = fs.readSync(descriptor, buffer, 0, buffer.length, null)) > 0) hash.update(buffer.subarray(0, count))
  } finally {
    fs.closeSync(descriptor)
  }
  return hash.digest('hex')
}

/** Hashes CLI sources, helpers and headers in the source installer's order. */
export function cliSourceFingerprint (sourceRoot, algorithm = 2) {
  assert.ok(algorithm === 1 || algorithm === 2, 'Unsupported CLI fingerprint algorithm')
  const hash = createHash('sha256')
  for (const relative of cliSourceFiles(sourceRoot, 'src/cli', algorithm === 2)) {
    hash.update(`${relative}:${digestFile(path.join(sourceRoot, relative))}\n`)
  }
  return hash.digest('hex')
}

function cliSourceFiles (root, directory = 'src/cli', setup = true) {
  const files = directory === 'src/cli' && setup
    ? ['bin/functions.sh', 'bin/android-functions.sh', 'bin/runtime-artifacts.sh']
    : []
  for (const entry of fs.readdirSync(path.join(root, directory), { withFileTypes: true })) {
    const relative = `${directory}/${entry.name}`
    if (entry.isDirectory()) files.push(...cliSourceFiles(root, relative, setup))
    else if (/\.(cc|hh|cjs|kt|xml|gradle|java)$/.test(entry.name)) files.push(relative)
  }
  return files.sort()
}

/** Verifies the compiled catalog/templates against the source being distributed. */
export function validateDistribution (metadata, sourceRoot) {
  assert.equal(metadata.schema, 1, 'Unsupported Android distribution schema')
  assert.equal(metadata.androidBridgeProtocol, 5, 'Android bridge protocol differs')
  assert.equal(metadata.cliSourceFingerprint, cliSourceFingerprint(sourceRoot, metadata.cliSourceFingerprintAlgorithm ?? 1),
    'Compiled CLI source fingerprint differs; rebuild the CLI before packaging')
  const source = fs.readFileSync(path.join(sourceRoot, 'src/cli/main.cc'), 'utf8')
  const catalog = source.match(/androidRuntimeSourceCatalog \(\) \{\s*return \{([\s\S]*?)\n {2}\};/)
  assert(catalog, 'Missing compiled Android source catalog')
  const expected = [...catalog[1].matchAll(/"(src\/[^"\n]+\.kt)"/g)].map(match => match[1]).sort()
  assert(expected.length > 0, 'Empty Android source catalog')
  assert.deepEqual(metadata.sources.map(entry => entry.path).sort(), expected,
    'Compiled CLI source catalog differs; rebuild the CLI before packaging')
  for (const entry of metadata.sources) {
    assert.equal(entry.sha256, digest(fs.readFileSync(path.join(sourceRoot, entry.path))),
      `Packaged Android source differs from the distribution snapshot: ${entry.path}`)
  }
  assert(Array.isArray(metadata.apis), 'Missing compiled Android API inventory')
  assert.deepEqual(metadata.apis.map(entry => entry.path).sort(), requiredApis.slice().sort(),
    'Compiled Android API inventory differs')
  for (const entry of metadata.apis) {
    assert.equal(entry.sha256, digestFile(path.join(sourceRoot, 'api', entry.path)),
      `Packaged Android API differs from the distribution snapshot: ${entry.path}`)
  }
  const templates = fs.readFileSync(path.join(sourceRoot, 'src/cli/templates.hh'), 'utf8')
  for (const [name, delimiter, key] of [
    ['gGradleBuildForSource', 'GROOVY', 'gradleTemplateSha256'],
    ['gAndroidManifest', 'XML', 'manifestTemplateSha256']
  ]) {
    const start = templates.indexOf(`constexpr auto ${name} = R"${delimiter}(`)
    assert(start >= 0, `Missing Android template: ${name}`)
    const body = start + `constexpr auto ${name} = R"${delimiter}(`.length
    const end = templates.indexOf(`)${delimiter}";`, body)
    assert(end > body, `Incomplete Android template: ${name}`)
    assert.equal(metadata[key], digest(templates.slice(body, end).replace(/\r\n/g, '\n')),
      `Compiled CLI template differs: ${name}; rebuild the CLI before packaging`)
  }
}

async function main () {
  const [runtime, api, source, output] = process.argv.slice(2)
  assert(runtime && api && source, 'Usage: check-android-distribution.js <runtime-root> <api-root> <source-root> [report.json]')
  const executable = path.join(path.resolve(runtime), 'bin', process.platform === 'win32' ? 'oroc.exe' : 'oroc')
  const metadata = await androidSdk.readAndroidSdk(executable, {
    env: { ...process.env, ORO_HOME: path.resolve(runtime), ORO_HOME_API: path.resolve(api) }
  })
  validateDistribution(metadata, path.resolve(source))
  assert.equal(cliSourceFingerprint(runtime, metadata.cliSourceFingerprintAlgorithm ?? 1), metadata.cliSourceFingerprint,
    'Packaged CLI sources differ from the compiled source fingerprint')
  const executablePath = path.relative(path.resolve(runtime), executable).split(path.sep).join('/')
  const files = { [executablePath]: digest(fs.readFileSync(executable)) }
  for (const relative of cliSourceFiles(runtime)) {
    files[relative] = digestFile(path.join(runtime, relative))
  }
  const nativeRoot = path.join(runtime, 'lib')
  const targets = fs.readdirSync(nativeRoot).filter(name => name.endsWith('-android')).sort()
  assert(targets.length > 0, 'Android distribution requires native target archives')
  for (const target of targets) {
    const archives = fs.readdirSync(path.join(nativeRoot, target)).filter(name => name.endsWith('.a')).sort()
    assert(archives.includes('liboro-runtime.a'), `Missing runtime archive: ${target}`)
    for (const name of archives) files[`lib/${target}/${name}`] = digestFile(path.join(nativeRoot, target, name))
  }
  for (const name of ['internal/runtime-contract.json', 'authentication.js', 'foreground.js', 'foreground/continuous.js', 'launcher.js', 'background/deferred.js', 'credentials.js', 'platform.js', 'notification.js', 'push.js', 'window/back.js', 'window/appearance.js']) {
    const bytes = fs.readFileSync(path.join(api, name))
    assert.equal(digest(bytes), digest(fs.readFileSync(path.join(source, 'api', name))), `Packaged API differs: ${name}`)
    files[`api/${name}`] = digest(bytes)
  }
  if (output) fs.writeFileSync(output, JSON.stringify({ ...metadata, setupHelpersFingerprintBound: metadata.cliSourceFingerprintAlgorithm === 2, files }, null, 2) + '\n')
  console.log(`Android distribution verified: ${metadata.sources.length} native sources, compiled templates and packaged API inputs`)
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) await main()
