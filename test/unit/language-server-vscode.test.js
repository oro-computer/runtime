import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync, mkdtempSync, mkdirSync, writeFileSync } from 'node:fs'
import * as fs from 'node:fs/promises'
import { tmpdir } from 'node:os'
import path from 'node:path'
import vm from 'node:vm'

test('VS Code starts one client per nearest Oro root and language, leaving ordinary projects alone', async () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro-vscode-'))
  const project = path.join(root, 'application [one]')
  const nested = path.join(project, 'nested')
  mkdirSync(nested, { recursive: true })
  writeFileSync(path.join(project, 'oro.toml'), '')
  writeFileSync(path.join(nested, 'oro.toml'), '')
  const document = directory => ({ languageId: 'kotlin', uri: { scheme: 'file', fsPath: path.join(directory, 'main.kt') } })
  const documents = [document(project), document(project), document(nested), document(root)]
  const clients = []
  let onOpen
  const settings = { languages: ['kotlin'], arguments: {}, environment: {}, executable: 'oroc' }
  const vscode = {
    workspace: {
      isTrusted: true,
      textDocuments: documents,
      getConfiguration: () => ({ get: name => settings[name] }),
      onDidOpenTextDocument: callback => { onOpen = callback; return {} },
      onDidChangeConfiguration: () => ({})
    },
    commands: { registerCommand: () => ({}) },
    window: { showErrorMessage: message => assert.fail(message) },
    Uri: { file: fsPath => ({ scheme: 'file', fsPath }) },
    RelativePattern: class { constructor (root, pattern) { this.root = root; this.pattern = pattern } }
  }
  const languageClient = {
    LanguageClient: class {
      constructor (id, name, server, options) { Object.assign(this, { id, server, options }); clients.push(this) }
      async start () { this.started = true }
      async dispose () { this.stopped = true }
    },
    ErrorAction: { Shutdown: 1 },
    CloseAction: { DoNotRestart: 1 }
  }
  const module = { exports: {} }
  vm.runInNewContext(readFileSync(new URL('../../editors/vscode/extension.cjs', import.meta.url), 'utf8'), {
    module,
    process,
    require: name => ({ vscode, 'vscode-languageclient/node': languageClient, 'node:fs/promises': fs, 'node:path': path })[name]
  })
  module.exports.activate({ subscriptions: [] })
  const deadline = Date.now() + 5000
  while (clients.filter(client => client.started).length < 2) {
    assert.ok(Date.now() < deadline)
    await new Promise(resolve => setTimeout(resolve, 10))
  }
  assert.equal(clients.length, 2)
  const outer = clients.find(client => client.server.options.cwd === project)
  assert.ok(outer.server.args.includes(path.join(project, 'oro.toml')))
  assert.equal(outer.options.initializationFailedHandler(), false)
  assert.equal(typeof outer.options.documentSelector[0].pattern, 'string')
  assert.ok(outer.options.documentSelector[0].pattern.includes('[[]one[]]'))
  assert.equal(typeof outer.options.middleware.provideOnTypeFormattingEdits, 'function')
  let forwarded = false
  await outer.options.middleware.provideHover(documents[2], {}, {}, () => { forwarded = true })
  assert.equal(forwarded, false)
  await outer.options.middleware.provideHover(documents[0], {}, {}, () => { forwarded = true })
  assert.equal(forwarded, true)
  vscode.workspace.isTrusted = false
  onOpen(document(path.join(root, 'untrusted')))
  await module.exports.deactivate()
  assert.ok(clients.every(client => client.stopped))
})

test('VS Code restart and deactivation wait for initialization before disposing clients', async () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro-vscode-lifecycle-'))
  writeFileSync(path.join(root, 'oro.toml'), '')
  const document = { languageId: 'kotlin', uri: { scheme: 'file', fsPath: path.join(root, 'main.kt') } }
  const clients = []
  let restart
  let onOpen
  let finishStarting
  const startup = new Promise(resolve => { finishStarting = resolve })
  const settings = { languages: ['kotlin'], arguments: {}, environment: {}, executable: 'oroc' }
  const vscode = {
    workspace: {
      isTrusted: true,
      textDocuments: [document],
      getConfiguration: () => ({ get: name => settings[name] }),
      onDidOpenTextDocument: callback => { onOpen = callback; return {} },
      onDidChangeConfiguration: () => ({})
    },
    commands: { registerCommand: (name, callback) => { restart = callback; return {} } },
    window: { showErrorMessage: message => assert.fail(message) },
    Uri: { file: fsPath => ({ scheme: 'file', fsPath }) }
  }
  const languageClient = {
    LanguageClient: class {
      constructor () { clients.push(this) }
      async start () { if (clients.length === 1) await startup; this.started = true }
      async dispose () { assert.equal(this.started, true, 'Disposal must wait for startup'); this.disposed = true }
    },
    ErrorAction: { Shutdown: 1 },
    CloseAction: { DoNotRestart: 1 }
  }
  const module = { exports: {} }
  vm.runInNewContext(readFileSync(new URL('../../editors/vscode/extension.cjs', import.meta.url), 'utf8'), {
    module, process, require: name => ({ vscode, 'vscode-languageclient/node': languageClient, 'node:fs/promises': fs, 'node:path': path })[name]
  })
  module.exports.activate({ subscriptions: [] })
  const deadline = Date.now() + 5000
  while (!clients.length) {
    assert.ok(Date.now() < deadline)
    await new Promise(resolve => setTimeout(resolve, 10))
  }
  const restarted = restart()
  const openedDuringRestart = onOpen(document)
  await new Promise(resolve => setTimeout(resolve, 10))
  assert.equal(clients.length, 1)
  assert.equal(clients[0].disposed, undefined)
  finishStarting()
  await Promise.all([restarted, openedDuringRestart])
  assert.equal(clients.length, 2)
  assert.equal(clients[0].disposed, true)
  await module.exports.deactivate()
  await onOpen(document)
  assert.equal(clients.length, 2)
  assert.ok(clients.every(client => client.disposed))
})
