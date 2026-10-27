// Real Gradle/LSP qualification. Run only after the packaged oroc toolchain is available.
import assert from 'node:assert/strict'
import { LspClient, qualifyEditing, editingCapabilities } from './lsp-client.js'
import { appendFileSync, copyFileSync, cpSync, existsSync, readdirSync, readFileSync, realpathSync, statSync, symlinkSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { execute } from '../../src/cli/tooling/process.cjs'
import { createQualificationDirectory } from '../../bin/qualification-directory.js'

const oroc = process.env.ORO_LANGUAGE_TEST_OROC || 'oroc'
const fixture = fileURLToPath(new URL('../fixtures/language-server', import.meta.url))

function position (text, token) {
  const offset = text.lastIndexOf(token) + 1
  assert.ok(offset > 0, token)
  const lines = text.slice(0, offset).split('\n')
  return { line: lines.length - 1, character: lines.at(-1).length }
}

async function qualifyLsp (root, variant, explicitRoot, environment = process.env) {
  const args = ['language-server', '--language', 'kotlin', '--variant', variant, '--offline']
  if (explicitRoot) args.push(root)
  const client = new LspClient(oroc, args, { cwd: tmpdir(), env: environment })
  const uri = pathToFileURL(path.join(root, 'android/Probe.kt')).href
  const siblingURI = pathToFileURL(path.join(root, 'android/Sibling.kt')).href
  const text = readFileSync(fileURLToPath(uri), 'utf8')
  try {
    const initialized = await client.request('initialize', {
      processId: process.pid,
      rootUri: pathToFileURL(root).href,
      workspaceFolders: [{ uri: pathToFileURL(root).href, name: 'Entire repository including history' }],
      capabilities: editingCapabilities
    })
    assert.match(initialized.serverInfo.name, /Oro Language Server/)
    assert.ok(client.messages.some(message => message.method === 'window/logMessage' && message.params.message === 'Indexing Kotlin dependencies'), 'The fwcd adapter must finish dependency indexing before initialization')
    client.send({ method: 'initialized', params: {} })
    client.send({ method: 'textDocument/didOpen', params: { textDocument: { uri, languageId: 'kotlin', version: 1, text } } })
    for (const token of ['Intent', 'NativeExtension', 'DeferredReadExecutor', 'ContextCompat', 'Sibling']) {
      process.stderr.write(`Checking Kotlin hover: ${token}\n`)
      const hover = await client.request('textDocument/hover', { textDocument: { uri }, position: position(text, token) })
      assert.ok(hover?.contents, `Missing ${token} hover`)
      assert.match(JSON.stringify(hover.contents), new RegExp(token))
    }
    const definition = await client.request('textDocument/definition', { textDocument: { uri }, position: position(text, 'Sibling') })
    const locations = Array.isArray(definition) ? definition : [definition]
    assert.ok(locations.some(location => (location?.uri || location?.targetUri) === siblingURI), JSON.stringify(definition))
    const diagnostics = await client.wait(message => message.method === 'textDocument/publishDiagnostics' && message.params.uri === uri)
    assert.deepEqual(diagnostics.params.diagnostics, [])
    for (const message of client.messages.filter(message => message.method === 'textDocument/publishDiagnostics')) assert.ok([uri, siblingURI].includes(message.params.uri), message.params.uri)
    assert.ok(!client.messages.some(message => message.method === 'window/showMessage' && message.params.type === 1))
    const { features } = await qualifyEditing(client, initialized.capabilities, uri, text, position(text, 'sibling:'), [uri, siblingURI], {
      language: 'kotlin',
      version: 1,
      diagnosticSuffix: '\nval oroEditingInvalid: Int = "bad"\n',
      symbol: 'sibling',
      unformatted: text.replace('import oro.runtime.app.NativeExtension\nimport oro.runtime.app.DeferredReadExecutor', 'import oro.runtime.app.DeferredReadExecutor\nimport oro.runtime.app.NativeExtension') + 'fun editorFormat( ) : Int {return 1}\n',
      broken: text.replace('import android.content.Intent\n', ''),
      actionTitle: /^Import android\.content\.Intent$/,
      fixedText: /import android\.content\.Intent\b/
    })
    process.stderr.write(`Kotlin editing features: ${JSON.stringify(features)}\n`)
  } finally { await client.close() }
}

for (const output of ['build', 'custom output/android artifacts']) {
  const root = createQualificationDirectory('oro-language-acceptance-')
  cpSync(fixture, root, { recursive: true })
  const configuration = path.join(root, 'oro.toml')
  writeFileSync(configuration, readFileSync(configuration, 'utf8').replace('output = "build"', `output = "${output}"`))
  // Invalid application code must not prevent SDK/resource preparation.
  const originalFile = path.join(root, 'android/Probe.kt')
  const originalText = readFileSync(originalFile, 'utf8')
  writeFileSync(originalFile, originalText + '\nfun invalidEditorSource( = broken\n')
  let previous
  for (const variant of ['devDebug', 'liveRelease']) {
    // Exporting a fresh model must run generated-source/classpath producers,
    // while leaving SDK class compilation for --prepare.
    const unprepared = JSON.parse(await execute(oroc, ['tooling', 'kotlin', '--variant', variant, root]))
    assert.equal(unprepared.prepared, false)
    assert.ok(unprepared.compileClasspath.length > 0)
    assert.ok(unprepared.compileClasspath.every(existsSync))
    assert.ok(unprepared.runtimeClasses.every(filename => !existsSync(filename)))
    for (const filename of unprepared.runtimeClasses) assert.ok(unprepared.missingArtifacts.includes(filename))
    const model = JSON.parse(await execute(oroc, ['tooling', 'kotlin', '--prepare', '--variant', variant, root]))
    assert.equal(model.generation, unprepared.generation)
    assert.equal(model.fingerprint, unprepared.fingerprint)
    assert.equal(model.variant, variant)
    assert.equal(model.schemaVersion, 1)
    assert.equal(model.originalSources.length, 2)
    assert.deepEqual(model.missingArtifacts, [])
    assert.ok(model.generation.startsWith(path.join(root, output)))
    if (previous) assert.notEqual(model.fingerprint, previous.fingerprint)
    // A new Gradle invocation must retain producer dependencies even when the
    // artifacts already exist; this is the editor's normal non-prepare launch.
    const offline = JSON.parse(await execute(oroc, ['tooling', 'kotlin', '--offline', '--variant', variant, root]))
    assert.equal(offline.prepared, false)
    assert.deepEqual(offline.missingArtifacts, [])
    assert.equal(offline.fingerprint, model.fingerprint)
    assert.equal(offline.generation, model.generation)
    writeFileSync(originalFile, originalText)
    // Exercise both initialization-based discovery from an unrelated cwd and
    // the explicit project override with the same real diagnostics/hover checks.
    await qualifyLsp(root, variant, variant === 'liveRelease')
    previous = model
  }
  if (output === 'build') {
    // Change a private SDK copy so runtime upgrades are tested without modifying
    // the installed SDK or any application. Libraries remain shared and read-only.
    const sdk = previous.sdk || process.env.ORO_HOME
    assert.ok(sdk, 'Runtime upgrade qualification requires ORO_HOME')
    const upgraded = createQualificationDirectory('oro-editor-upgraded-sdk-')
    for (const name of readdirSync(sdk)) {
      const source = path.join(sdk, name)
      const destination = path.join(upgraded, name)
      if (name === 'src') cpSync(source, destination, { recursive: true, dereference: true })
      else if (statSync(source).isDirectory()) symlinkSync(realpathSync(source), destination, process.platform === 'win32' ? 'junction' : 'dir')
      else copyFileSync(realpathSync(source), destination)
    }
    const runtimeFile = previous.runtime.sources.find(source => source.path.endsWith('.kt')).path
    appendFileSync(path.join(upgraded, runtimeFile), '\n// Editor qualification SDK revision.\n')
    const environment = { ...process.env, ORO_HOME: upgraded }
    const refreshed = JSON.parse(await execute(oroc, ['tooling', 'kotlin', '--prepare', '--offline', '--variant', previous.variant, root], { env: environment }))
    assert.notEqual(refreshed.fingerprint, previous.fingerprint)
    assert.notEqual(refreshed.generation, previous.generation)
    assert.deepEqual(refreshed.missingArtifacts, [])
    await qualifyLsp(root, previous.variant, true, environment)
    // A deliberately absent fixed dependency must produce offline recovery guidance.
    const valid = readFileSync(configuration, 'utf8')
    writeFileSync(configuration, valid + '\ndependencies = "computer.oro.editor:missing-qualification-dependency:1.0.0"\n')
    try {
      await assert.rejects(execute(oroc, ['tooling', 'kotlin', '--prepare', '--offline', root]), error => /offline/i.test(error.message) && /uncached dependency/.test(error.message))
    } finally { writeFileSync(configuration, valid) }
  }
  process.stderr.write(`Qualified original-source diagnostics, hover and navigation: ${root}\n`)
}
