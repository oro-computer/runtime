'use strict'

const { Messages, encode } = require('../../../src/cli/tooling/lsp.cjs')
const documents = new Map()
let indexed = !process.env.ORO_TEST_INDEX_DELAY
const send = message => {
  if (process.env.ORO_TEST_ENCODED_URIS && message.method === 'textDocument/publishDiagnostics') {
    message.params.uri = message.params.uri.replace('main.kt', '%6dain.kt').replace(/^file:\/\/\/([a-z]):/i, (_, drive) => `file:///${drive.toLowerCase()}%3A`)
  }
  process.stdout.write(encode({ jsonrpc: '2.0', ...message }))
}
const input = new Messages(message => {
  if (message.method === 'initialize') {
    send({ id: message.id, result: { capabilities: { textDocumentSync: 2, hoverProvider: true, renameProvider: true, completionProvider: {} } } })
    if (!indexed) send({ id: 'index-progress', method: 'window/workDoneProgress/create', params: { token: 'indexing' } })
  }
  if (message.id === 'index-progress' && !message.method) {
    send({ method: '$/progress', params: { token: 'indexing', value: { kind: 'begin', title: 'Kotlin: Indexing' } } })
    setTimeout(() => {
      indexed = true
      send({ method: '$/progress', params: { token: 'indexing', value: { kind: 'end' } } })
    }, Number(process.env.ORO_TEST_INDEX_DELAY))
  }
  if (message.method === 'textDocument/didOpen') {
    documents.set(message.params.textDocument.uri, message.params.textDocument)
    send({ method: 'textDocument/publishDiagnostics', params: { uri: message.params.textDocument.uri, version: message.params.textDocument.version, diagnostics: [] } })
  }
  if (message.method === 'textDocument/didChange') {
    const document = documents.get(message.params.textDocument.uri)
    const { updateDocument } = require('../../../src/cli/tooling/session.cjs')
    documents.set(document.uri, updateDocument(document, message.params))
    if (process.env.ORO_TEST_STALE_DIAGNOSTICS) send({ method: 'textDocument/publishDiagnostics', params: { uri: document.uri, version: document.version, diagnostics: [{ message: 'stale', range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } } }] } })
  }
  if (message.method === 'textDocument/hover') {
    if (!indexed) { send({ id: message.id, error: { code: -32603, message: 'Analysis raced dependency indexing' } }); return [] }
    const result = { contents: { kind: 'plaintext', value: documents.get(message.params.textDocument.uri)?.text || '' } }
    setTimeout(() => send({ id: message.id, result }), Number(process.env.ORO_TEST_HOVER_DELAY || 0))
  }
  if (message.method === 'textDocument/completion') send({ id: message.id, result: [{ label: 'sample' }] })
  if (message.method === 'textDocument/rename') send({ id: message.id, result: { changes: { 'file:///outside/Generated.kt': [] } } })
  if (message.method === 'shutdown') send({ id: message.id, result: null })
  if (message.method === 'exit') process.exit(0)
  return []
})
input.on('error', () => process.exit(1))
process.stdin.pipe(input).resume()
