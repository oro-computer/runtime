'use strict'

const vscode = require('vscode')
const fs = require('node:fs/promises')
const path = require('node:path')
const { LanguageClient, ErrorAction, CloseAction } = require('vscode-languageclient/node')
/** @type {Map<string, {client: import('vscode-languageclient/node').LanguageClient, started: Promise<void>}>} */
const clients = new Map()
const families = { kotlin: 'kotlin', javascript: 'typescript', javascriptreact: 'typescript', typescript: 'typescript', typescriptreact: 'typescript', c: 'cpp', cpp: 'cpp', 'objective-c': 'cpp', 'objective-cpp': 'cpp', swift: 'swift' }
let restarting
let stopping = false

/** @param {import('vscode').TextDocument|{uri: import('vscode').Uri}} document */
async function configurationFor (document) {
  if (document.uri.scheme !== 'file') return null
  let directory = path.dirname(document.uri.fsPath)
  for (;;) {
    for (const name of ['oro.toml', 'oro.ini']) {
      const file = path.join(directory, name)
      try { if ((await fs.stat(file)).isFile()) return file } catch (error) { if (error.code !== 'ENOENT') throw error }
    }
    const parent = path.dirname(directory)
    if (parent === directory) return null
    directory = parent
  }
}

/** @param {import('vscode').TextDocument} document */
async function open (document) {
  if (restarting) await restarting
  if (stopping || !vscode.workspace.isTrusted) return
  const family = families[document.languageId]
  if (!family) return
  if (process.platform === 'win32' && family === 'cpp') return
  const configuration = await configurationFor(document)
  if (!configuration || stopping) return
  if (restarting) { await restarting; return open(document) }
  const root = path.dirname(configuration)
  const settings = vscode.workspace.getConfiguration('oro.languageServer', vscode.Uri.file(root))
  if (!settings.get('languages').includes(family)) return
  const id = `${configuration}:${family}`
  if (clients.has(id)) return
  const args = settings.get('arguments')[family] || []
  if (!Array.isArray(args) || args.some(arg => typeof arg !== 'string')) throw new Error(`oro.languageServer.arguments.${family} must be a string array`)
  const environment = settings.get('environment')
  if (Object.values(environment).some(value => typeof value !== 'string')) throw new Error('oro.languageServer.environment values must be strings')
  // Nested Oro projects share workspace folders but never document ownership.
  /** @type {import('vscode-languageclient/node').Middleware} */
  const middleware = {}
  /** @type {Array<keyof import('vscode-languageclient/node').Middleware>} */
  const documentMethods = ['didOpen', 'didChange', 'didClose', 'didSave', 'willSave', 'willSaveWaitUntil', 'provideHover', 'provideCompletionItem', 'provideSignatureHelp', 'provideDefinition', 'provideTypeDefinition', 'provideImplementation', 'provideDeclaration', 'provideReferences', 'provideDocumentHighlights', 'provideDocumentSymbols', 'provideCodeActions', 'provideCodeLenses', 'provideDocumentColors', 'provideInlineValues', 'provideLinkedEditingRange', 'provideDocumentFormattingEdits', 'provideDocumentRangeFormattingEdits', 'provideOnTypeFormattingEdits', 'provideRenameEdits', 'prepareRename', 'provideDocumentLinks', 'provideFoldingRanges', 'provideSelectionRanges', 'provideInlayHints', 'provideDocumentSemanticTokens', 'provideDocumentSemanticTokensEdits', 'provideDocumentRangeSemanticTokens', 'prepareCallHierarchy', 'prepareTypeHierarchy']
  for (const name of documentMethods) {
    middleware[name] = async (...values) => {
      const first = values[0].document || values[0]
      if (await configurationFor(first) !== configuration) return undefined
      const next = values.pop()
      return next(...values)
    }
  }
  // LanguageClient uses the protocol's string glob, not VS Code's RelativePattern.
  const pattern = root.split(path.sep).join('/').replace(/[?*{}[\]]/g, character => `[${character}]`) + '/**/*'
  const client = new LanguageClient(id, `Oro ${family} (${path.basename(root)})`, {
    command: settings.get('executable'),
    args: ['language-server', '--language', family, ...args, '--config', configuration],
    options: { cwd: root, env: { ...process.env, ...environment } }
  }, {
    documentSelector: Object.entries(families).filter(([, value]) => value === family).map(([language]) => ({ scheme: 'file', language, pattern })),
    workspaceFolder: { uri: vscode.Uri.file(root), name: path.basename(root), index: 0 },
    middleware,
    initializationFailedHandler: () => false,
    errorHandler: { error: () => ({ action: ErrorAction.Shutdown }), closed: () => ({ action: CloseAction.DoNotRestart }) }
  })
  const entry = { client, started: Promise.resolve() }
  clients.set(id, entry)
  entry.started = client.start().catch(error => { vscode.window.showErrorMessage(`Oro ${family}: ${error.message}. Fix the reported cause, then run Oro: Restart Language Servers.`) })
  await entry.started
}

async function disposeClients () {
  const entries = [...clients.values()]
  clients.clear()
  const results = await Promise.allSettled(entries.map(async entry => {
    // The official client cannot stop while initialize is still pending.
    await entry.started
    await entry.client.dispose()
  }))
  for (const result of results) if (result.status === 'rejected') vscode.window.showErrorMessage(`Oro language client shutdown: ${result.reason.message}`)
}

async function restart () {
  if (restarting) return restarting
  restarting = disposeClients()
  try { await restarting } finally { restarting = null }
  for (const document of vscode.workspace.textDocuments) await open(document)
}

/**
 * Activate Oro clients only for documents beneath an Oro project marker.
 * @param {import('vscode').ExtensionContext} context
 */
function activate (context) {
  stopping = false
  const onOpen = document => open(document).catch(error => vscode.window.showErrorMessage(error.message))
  context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(onOpen))
  context.subscriptions.push(vscode.commands.registerCommand('oro.languageServer.restart', restart))
  context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(event => { if (event.affectsConfiguration('oro.languageServer')) restart().catch(error => vscode.window.showErrorMessage(error.message)) }))
  vscode.workspace.textDocuments.forEach(onOpen)
}

/** Stop all managed language clients when the extension host shuts down. */
async function deactivate () { stopping = true; if (restarting) await restarting; await disposeClients() }

module.exports = { activate, deactivate }
