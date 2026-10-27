import test from 'node:test'
import assert from 'node:assert/strict'
import { PassThrough } from 'node:stream'
import { mkdtempSync, writeFileSync, mkdirSync, existsSync, readFileSync, symlinkSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { initializeClient, Messages, encode } from '../../src/cli/tooling/lsp.cjs'
import { serve, updateDocument } from '../../src/cli/tooling/session.cjs'
import { scopeGuard } from '../../src/cli/tooling/scope.cjs'
import { watchModel } from '../../src/cli/tooling/watch.cjs'
import { execute, spawnManaged } from '../../src/cli/tooling/process.cjs'
import { locked, lease, retain } from '../../src/cli/tooling/cache.cjs'
import { jetbrainsWorkspace } from '../../src/cli/tooling/kotlin-workspace.cjs'

const backend = fileURLToPath(new URL('../fixtures/language-server/backend.cjs', import.meta.url))

async function connection (prepare, options) {
  const source = new PassThrough()
  const output = new PassThrough()
  const messages = []
  const parser = new Messages(message => { messages.push(message); return [] })
  output.pipe(parser).resume()
  const initialization = initializeClient(source)
  source.write(encode({ jsonrpc: '2.0', id: 1, method: 'initialize', params: { capabilities: {}, workDoneToken: 'startup' } }))
  const client = await initialization
  const done = serve(client, prepare, { ...options, output }).finally(() => client.close())
  const send = message => source.write(encode({ jsonrpc: '2.0', ...message }))
  const wait = async predicate => {
    const deadline = Date.now() + 10000
    for (;;) {
      const message = messages.find(predicate)
      if (message) return message
      if (Date.now() > deadline) throw new Error(`Missing LSP message: ${JSON.stringify(messages)}`)
      await new Promise(resolve => setTimeout(resolve, 10))
    }
  }
  return { source, output, send, wait, done, messages }
}

function launch (extra = {}) {
  return { command: process.execPath, args: [backend], cwd: tmpdir(), env: process.env, language: 'test', ...extra }
}

test('initialization errors are structured, non-retryable and close cleanly', async () => {
  const client = await connection(async () => { throw new Error('Install the selected Android SDK') })
  try {
    const message = await client.wait(message => message.id === 1)
    assert.equal(message.error.data.retry, false)
    assert.match(message.error.message, /Android SDK/)
    assert.ok(client.messages.some(message => message.method === '$/progress'))
  } finally { client.source.end(); await client.done }
})

test('Kotlin analysis waits for the backend dependency index to finish', async () => {
  const client = await connection(async () => launch({ initializationProgressTitle: 'Kotlin: Indexing', env: { ...process.env, ORO_TEST_INDEX_DELAY: '100' } }))
  const uri = pathToFileURL(path.join(tmpdir(), 'oro-indexing.kt')).href
  try {
    await client.wait(message => message.id === 1)
    client.send({ method: 'textDocument/didOpen', params: { textDocument: { uri, languageId: 'kotlin', version: 1, text: 'val x = 1' } } })
    client.send({ id: 2, method: 'textDocument/hover', params: { textDocument: { uri }, position: { line: 0, character: 4 } } })
    assert.equal((await client.wait(message => message.id === 2)).result.contents.value, 'val x = 1')
    assert.ok(!client.messages.some(message => message.method === 'window/workDoneProgress/create'))
  } finally { client.send({ method: 'exit' }); await client.done }
})

test('closing the editor cancels an in-flight preparation process', async () => {
  let running
  let finishStart
  const started = new Promise(resolve => { finishStart = resolve })
  const client = await connection(async () => {
    running = execute(process.execPath, ['-e', 'setInterval(() => {}, 1000)'])
    finishStart()
    await running
    return launch()
  })
  await started
  client.source.end()
  await assert.rejects(running, /disconnected/)
  await client.done
})

test('startup deadline returns one actionable error and cancels preparation', async () => {
  const client = await connection(async () => {
    await execute(process.execPath, ['-e', 'setInterval(() => {}, 1000)'])
    return launch()
  }, { startupTimeout: 100 })
  try {
    const message = await client.wait(message => message.id === 1)
    assert.match(message.error.message, /startup exceeded/)
    await client.done
    assert.equal(client.messages.filter(message => message.id === 1).length, 1)
  } finally { client.source.end(); await client.done }
})

test('stopping a launcher also reaps a descendant that ignores termination', async () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro-lsp-process-'))
  const heartbeat = path.join(root, 'heartbeat')
  const descendant = `const fs = require('node:fs'); process.on('SIGTERM', () => {}); setInterval(() => fs.writeFileSync(${JSON.stringify(heartbeat)}, String(Date.now())), 50)`
  const program = `require('node:child_process').spawn(process.execPath, ['-e', ${JSON.stringify(descendant)}], { stdio: 'ignore' }); setInterval(() => {}, 1000)`
  const managed = spawnManaged(process.execPath, ['-e', program], { stdio: 'ignore' })
  try {
    const deadline = Date.now() + 5000
    while (!existsSync(heartbeat)) {
      assert.ok(Date.now() < deadline, 'Descendant did not start')
      await new Promise(resolve => setTimeout(resolve, 20))
    }
    managed.stop()
    await managed.finished
    const stopped = readFileSync(heartbeat, 'utf8')
    await new Promise(resolve => setTimeout(resolve, 150))
    assert.equal(readFileSync(heartbeat, 'utf8'), stopped)
  } finally { managed.stop(); await managed.finished }
})

test('responses computed for an old buffer version are rejected', async () => {
  const client = await connection(async () => launch({ env: { ...process.env, ORO_TEST_HOVER_DELAY: '100' } }))
  const uri = pathToFileURL(path.join(tmpdir(), 'oro-stale.kt')).href
  try {
    await client.wait(message => message.id === 1)
    client.send({ method: 'textDocument/didOpen', params: { textDocument: { uri, languageId: 'kotlin', version: 1, text: 'val x = 1' } } })
    client.send({ id: 2, method: 'textDocument/hover', params: { textDocument: { uri }, position: { line: 0, character: 4 } } })
    client.send({ method: 'textDocument/didChange', params: { textDocument: { uri, version: 2 }, contentChanges: [{ text: 'val x = 2' }] } })
    assert.equal((await client.wait(message => message.id === 2)).error.code, -32801)
  } finally { client.send({ method: 'exit' }); await client.done }
})

test('native header edits follow project include directories from compilation commands', () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro-header-scope-'))
  const header = path.join(root, 'include', 'nested', 'types.hh')
  mkdirSync(path.dirname(header), { recursive: true })
  writeFileSync(header, 'struct Example {};')
  const scope = scopeGuard({ language: 'cpp', projectRoot: root, originalSources: [path.join(root, 'main.cc')], headerRoots: [path.join(root, 'include')] })
  assert.ok(scope.accepts(pathToFileURL(header).href))
  assert.ok(!scope.accepts(pathToFileURL(path.join(root, 'history', 'types.hh')).href))
})

test('diagnostics preserve editor symlink URIs and discard stale versions', async () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro-lsp-alias-'))
  const original = path.join(root, 'original')
  const alias = path.join(root, 'editor')
  mkdirSync(original)
  symlinkSync(original, alias, process.platform === 'win32' ? 'junction' : 'dir')
  const file = path.join(original, 'main.kt')
  writeFileSync(file, 'val x = 1')
  const uri = pathToFileURL(path.join(alias, 'main.kt')).href
  const client = await connection(async () => launch({
    ...scopeGuard({ language: 'kotlin', projectRoot: root, originalSources: [file] }),
    env: { ...process.env, ORO_TEST_STALE_DIAGNOSTICS: '1' }
  }))
  try {
    await client.wait(message => message.id === 1)
    client.send({ method: 'textDocument/didOpen', params: { textDocument: { uri, languageId: 'kotlin', version: 1, text: 'val x = 1' } } })
    assert.equal((await client.wait(message => message.method === 'textDocument/publishDiagnostics')).params.uri, uri)
    client.send({ method: 'textDocument/didChange', params: { textDocument: { uri, version: 2 }, contentChanges: [{ text: 'val x = 2' }] } })
    client.send({ id: 2, method: 'textDocument/hover', params: { textDocument: { uri }, position: { line: 0, character: 4 } } })
    await client.wait(message => message.id === 2)
    assert.equal(client.messages.filter(message => message.method === 'textDocument/publishDiagnostics').length, 1)
  } finally { client.send({ method: 'exit' }); await client.done }
})

test('refresh retains incremental unsaved UTF-16 buffers and keeps editor request IDs', async () => {
  let stamp = 0
  let launches = 0
  const client = await connection(async () => { launches++; return launch({ watch: () => stamp }) })
  const uri = pathToFileURL(path.join(tmpdir(), 'oro-unsaved.kt')).href
  try {
    await client.wait(message => message.id === 1)
    client.send({ method: 'initialized', params: {} })
    client.send({ method: 'textDocument/didOpen', params: { textDocument: { uri, languageId: 'kotlin', version: 1, text: 'val x = "😀"\r\nval y = 1\r\n' } } })
    client.send({ method: 'textDocument/didChange', params: { textDocument: { uri, version: 2 }, contentChanges: [{ range: { start: { line: 1, character: 8 }, end: { line: 1, character: 9 } }, text: '42' }] } })
    stamp++
    const deadline = Date.now() + 10000
    for (;;) {
      if (launches >= 2 && client.messages.some(message => message.params?.version === 2)) break
      if (Date.now() > deadline) throw new Error('Refresh did not replay the unsaved buffer')
      await new Promise(resolve => setTimeout(resolve, 20))
    }
    client.send({ id: 'hover', method: 'textDocument/hover', params: { textDocument: { uri }, position: { line: 1, character: 4 } } })
    assert.match((await client.wait(message => message.id === 'hover')).result.contents.value, /val y = 42/)
    assert.equal(client.messages.filter(message => message.id === 1).length, 1)
    client.send({ id: 4, method: 'shutdown' })
    assert.equal((await client.wait(message => message.id === 4)).result, null)
  } finally { client.send({ method: 'exit' }); await client.done }
})

test('backend edits outside original sources are rejected without losing the connection', async () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro-lsp-scope-'))
  const file = path.join(root, 'main.kt')
  writeFileSync(file, 'val x = 1')
  const uri = pathToFileURL(file).href
  const client = await connection(async () => launch(scopeGuard({ projectRoot: root, originalSources: [file], language: 'kotlin' })))
  try {
    await client.wait(message => message.id === 1)
    client.send({ method: 'textDocument/didOpen', params: { textDocument: { uri, languageId: 'kotlin', version: 1, text: 'val x = 1' } } })
    client.send({ id: 2, method: 'textDocument/rename', params: { textDocument: { uri }, position: { line: 0, character: 4 }, newName: 'y' } })
    assert.match((await client.wait(message => message.id === 2)).error.message, /outside the declared source scope/)
    client.send({ id: 3, method: 'textDocument/completion', params: { textDocument: { uri }, position: { line: 0, character: 4 } } })
    assert.equal((await client.wait(message => message.id === 3)).result[0].label, 'sample')
  } finally { client.send({ method: 'exit' }); await client.done }
})

test('watching detects dependency replacement and source removal without reacting to typing', () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro-lsp-watch-'))
  const source = path.join(root, 'source.kt')
  const library = path.join(root, 'library.jar')
  writeFileSync(source, 'one')
  writeFileSync(library, 'one')
  const watch = watchModel({ originalSources: [source], compileClasspath: [library] })
  const initial = watch()
  writeFileSync(source, 'two')
  assert.equal(watch(), initial)
  writeFileSync(library, 'changed dependency')
  assert.notEqual(watch(), initial)
})

test('cache preparation serializes concurrent callers and protects leased generations', async () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro-lsp-cache-'))
  const order = []
  await Promise.all([locked(root, async () => { order.push('first'); await new Promise(resolve => setTimeout(resolve, 100)); order.push('first-end') }), locked(root, async () => { order.push('second') })])
  assert.deepEqual(order, ['first', 'first-end', 'second'])
  const releases = []
  for (let index = 0; index < 12; index++) {
    const generation = path.join(root, String(index))
    mkdirSync(generation)
    const release = lease({ projectRoot: root, generation })
    if (index === 0) releases.push(release)
    else release()
  }
  retain(root)
  assert.ok(existsSync(path.join(root, '0')))
  releases.forEach(release => release())
})

test('Kotlin JSON workspace excludes historical sources and uses original file locations', () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro-kotlin-workspace-'))
  const source = path.join(root, 'Current.kt')
  writeFileSync(source, 'class Current')
  mkdirSync(path.join(root, 'history'))
  writeFileSync(path.join(root, 'history', 'Current.kt'), 'class Current')
  const workspace = jetbrainsWorkspace({ originalSources: [source], compileClasspath: ['/sdk/android.jar'], runtimeClasses: ['/sdk/runtime'], jdk: { compilerVersion: 21, compilerHome: '/jdk' }, compiler: { javaSource: '21', jvmTarget: '21', freeCompilerArgs: [] } })
  assert.equal(workspace.modules[0].contentRoots[0].sourceRoots[0].path, root)
  assert.deepEqual(workspace.modules[0].contentRoots[0].excludedUrls, [pathToFileURL(path.join(root, 'history')).href])
  assert.equal(workspace.libraries.length, 2)
})

test('document edits reject stale versions and invalid UTF-16 ranges', () => {
  const document = { version: 2, text: '😀\n' }
  assert.throws(() => updateDocument(document, { textDocument: { version: 2 }, contentChanges: [] }), /increase/)
  assert.throws(() => updateDocument(document, { textDocument: { version: 3 }, contentChanges: [{ range: { start: { line: 0, character: 3 }, end: { line: 0, character: 4 } }, text: '' }] }), /outside/)
})
