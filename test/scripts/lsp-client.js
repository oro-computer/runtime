import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { setTimeout as delay } from 'node:timers/promises'
import { Messages, encode } from '../../src/cli/tooling/lsp.cjs'

/** A real stdio client for qualification of installed language servers. */
export class LspClient {
  constructor (command, args, options = {}) {
    this.messages = []
    this.pending = new Map()
    this.sequence = 0
    this.stderr = ''
    this.child = spawn(command, args, { ...options, stdio: ['pipe', 'pipe', 'pipe'] })
    this.exited = new Promise(resolve => this.child.once('close', resolve))
    const fail = error => {
      this.failure = error
      for (const request of this.pending.values()) { clearTimeout(request.timer); request.reject(error) }
      this.pending.clear()
    }
    this.child.on('error', fail)
    this.child.stdin.on('error', fail)
    this.child.once('close', code => fail(new Error(`LSP exited (${code}): ${this.stderr}`)))
    this.child.stderr.on('data', chunk => { this.stderr = (this.stderr + chunk).slice(-32000) })
    const parser = new Messages(message => {
      this.messages.push(message)
      if (message.method && message.id != null) this.send({ id: message.id, result: message.method === 'workspace/configuration' ? message.params.items.map(() => ({})) : null })
      else if (message.id != null && this.pending.has(message.id)) {
        const request = this.pending.get(message.id)
        this.pending.delete(message.id)
        clearTimeout(request.timer)
        if (message.error) request.reject(Object.assign(new Error(message.error.message), { code: message.error.code }))
        else request.resolve(message.result)
      }
      return []
    })
    parser.on('error', fail)
    this.child.stdout.pipe(parser).resume()
  }

  send (message) { this.child.stdin.write(encode({ jsonrpc: '2.0', ...message })) }

  async request (method, params, attempt = 0) {
    if (this.failure) return Promise.reject(this.failure)
    const id = ++this.sequence
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending.delete(id); reject(new Error(`Timed out: ${method}\n${this.stderr}`)) }, 300000)
      this.pending.set(id, { resolve, reject, timer })
      this.send({ id, method, params })
    }).catch(async error => {
      if (error.code !== -32802 || attempt >= 60) throw error
      await delay(1000)
      return this.request(method, params, attempt + 1)
    })
  }

  async wait (predicate, timeout = 60000) {
    const deadline = Date.now() + timeout
    for (;;) {
      const message = this.messages.find(predicate)
      if (message) return message
      if (this.failure) throw this.failure
      if (Date.now() > deadline) throw new Error(`LSP notification timed out\n${this.stderr}`)
      await delay(50)
    }
  }

  async close () {
    try { if (!this.failure) { await this.request('shutdown', null); this.send({ method: 'exit' }) } } finally {
      this.child.stdin.end()
      const force = setTimeout(() => this.child.kill('SIGKILL'), 5000)
      await this.exited
      clearTimeout(force)
    }
  }
}

/** Client capabilities used by real editing qualification. */
export const editingCapabilities = {
  workspace: { workspaceEdit: { documentChanges: true } },
  textDocument: {
    publishDiagnostics: { versionSupport: true },
    codeAction: {
      codeActionLiteralSupport: { codeActionKind: { valueSet: ['quickfix'] } },
      resolveSupport: { properties: ['edit'] }
    }
  }
}

function offset (text, position) {
  assert.ok(Number.isInteger(position.line) && position.line >= 0, 'Invalid edit line')
  assert.ok(Number.isInteger(position.character) && position.character >= 0, 'Invalid edit character')
  let start = 0
  for (let line = 0; line < position.line; line++) {
    const end = text.indexOf('\n', start)
    assert.ok(end >= 0, 'Edit line is outside the document')
    start = end + 1
  }
  let end = text.indexOf('\n', start)
  if (end < 0) end = text.length
  if (text[end - 1] === '\r') end--
  assert.ok(position.character <= end - start, 'Edit character is outside the line')
  return start + position.character
}

/**
 * Apply simultaneous LSP TextEdits in UTF-16 coordinates, rejecting overlap.
 * @param {string} text Original document.
 * @param {object[]} edits TextEdits in server order.
 * @returns {string} Edited document.
 */
export function applyTextEdits (text, edits) {
  assert.ok(Array.isArray(edits), 'Expected TextEdits')
  const ordered = edits.map((edit, index) => {
    assert.equal(typeof edit.newText, 'string')
    const start = offset(text, edit.range.start)
    const end = offset(text, edit.range.end)
    assert.ok(end >= start, 'Reversed edit range')
    return { start, end, text: edit.newText, index }
  }).sort((left, right) => left.start - right.start || left.index - right.index)
  let previousEnd = 0
  for (const edit of ordered) {
    assert.ok(edit.start >= previousEnd, 'Overlapping text edits')
    previousEnd = edit.end
  }
  for (const edit of ordered.reverse()) text = text.slice(0, edit.start) + edit.text + text.slice(edit.end)
  return text
}

/**
 * @typedef {{text: string, version: number|null}} EditorDocument
 */

/**
 * Apply WorkspaceEdit text changes only to known original documents.
 * @param {Map<string, EditorDocument>} documents Current original buffers.
 * @param {object} edit LSP WorkspaceEdit.
 * @returns {Map<string, EditorDocument>} Result without modifying the input.
 */
export function applyWorkspaceEdit (documents, edit) {
  assert.ok(edit && typeof edit === 'object', 'Missing WorkspaceEdit')
  const result = new Map(documents)
  const changes = edit.documentChanges || Object.entries(edit.changes || {}).map(([uri, edits]) => ({ textDocument: { uri, version: null }, edits }))
  for (const change of changes) {
    assert.ok(change.textDocument && !change.kind, 'Unexpected resource operation in editing fixture')
    const { uri, version } = change.textDocument
    assert.ok(result.has(uri), `Edit targets a generated or unrelated file: ${uri}`)
    const document = result.get(uri)
    if (version != null) assert.equal(version, document.version, 'Edit targets an outdated document version')
    result.set(uri, { ...document, text: applyTextEdits(document.text, change.edits) })
  }
  return result
}

/**
 * @typedef {object} EditingFixture
 * @property {string} language LSP language identifier.
 * @property {number} version Current primary document version.
 * @property {string} symbol Identifier whose declaration and uses must be renamed.
 * @property {string} unformatted Valid source deliberately requiring formatting.
 * @property {string} broken Source containing a diagnostic with a known quick fix.
 * @property {string} diagnosticSuffix Temporary error used to force a diagnostic transition.
 * @property {RegExp} actionTitle Expected quick-fix title.
 * @property {RegExp} fixedText Required source after applying the quick fix.
 */

/**
 * Apply real backend edits to unsaved originals and verify their resulting diagnostics.
 * @param {LspClient} client Connected client with the primary document open.
 * @param {object} capabilities Advertised server capabilities.
 * @param {string} uri Primary original URI.
 * @param {string} text Primary original source.
 * @param {object} position Position within the rename declaration.
 * @param {string[]} allowed Original file URIs.
 * @param {EditingFixture} fixture Language-specific expected edits.
 * @returns {Promise<{features: object, version: number}>} Results and restored buffer version.
 */
export async function qualifyEditing (client, capabilities, uri, text, position, allowed, fixture) {
  const document = { textDocument: { uri }, position }
  const features = {}
  const originals = new Map(allowed.map(target => [target, readFileSync(fileURLToPath(target), 'utf8')]))
  const documents = new Map([...originals].map(([target, source]) => [target, { text: target === uri ? text : source, version: target === uri ? fixture.version : null }]))
  const diagnosticsSince = (target, after, predicate) => client.wait(message => message.method === 'textDocument/publishDiagnostics' && client.messages.indexOf(message) >= after && message.params.uri === target && (message.params.version == null || message.params.version === documents.get(target).version) && predicate(message.params.diagnostics))
  const change = (target, source) => {
    const previous = documents.get(target)
    const version = (previous.version || 0) + 1
    documents.set(target, { text: source, version })
    client.send(previous.version == null
      ? { method: 'textDocument/didOpen', params: { textDocument: { uri: target, languageId: fixture.language, version, text: source } } }
      : { method: 'textDocument/didChange', params: { textDocument: { uri: target, version }, contentChanges: [{ text: source }] } })
  }
  const apply = async result => {
    const changed = [...result].filter(([target, value]) => value.text !== documents.get(target).text)
    assert.ok(changed.length, 'Backend returned no effective edits')
    // Servers may suppress repeated empty diagnostics. Establish an error first,
    // then require it to clear when the actual edited buffer is analyzed.
    let after = client.messages.length
    for (const [target, value] of changed) change(target, value.text + fixture.diagnosticSuffix)
    await Promise.all(changed.map(([target]) => diagnosticsSince(target, after, diagnostics => diagnostics.some(item => item.severity === 1))))
    after = client.messages.length
    for (const [target, value] of changed) change(target, value.text)
    await Promise.all(changed.map(([target]) => diagnosticsSince(target, after, diagnostics => !diagnostics.some(item => item.severity === 1))))
  }
  const restore = async () => {
    const restored = new Map([...documents].map(([target, value]) => [target, { ...value, text: target === uri ? text : originals.get(target) }]))
    if ([...restored].some(([target, value]) => value.text !== documents.get(target).text)) await apply(restored)
  }
  try {
    if (capabilities.completionProvider) {
      process.stderr.write(`Checking completion edits: ${uri}\n`)
      const result = await client.request('textDocument/completion', document)
      assert.ok(result && (Array.isArray(result) || Array.isArray(result.items)), 'Missing completion response')
      features.completion = 'passed'
    } else features.completion = 'not advertised by backend'
    if (capabilities.referencesProvider) {
      process.stderr.write(`Checking references edits: ${uri}\n`)
      const result = await client.request('textDocument/references', { ...document, context: { includeDeclaration: true } })
      assert.ok(Array.isArray(result) && result.some(item => item.uri === uri), 'References must include an original source location')
      for (const location of result) {
        assert.ok(documents.has(location.uri), 'Reference targets an unrelated source')
        const source = documents.get(location.uri).text
        assert.equal(source.slice(offset(source, location.range.start), offset(source, location.range.end)), fixture.symbol)
      }
      features.references = 'passed'
    } else features.references = 'not advertised by backend'
    if (capabilities.renameProvider) {
      process.stderr.write(`Checking rename edits: ${uri}\n`)
      const renamed = await client.request('textDocument/rename', { ...document, newName: 'editorRenamed' })
      const result = applyWorkspaceEdit(documents, renamed)
      const symbol = new RegExp(`\\b${fixture.symbol}\\b`, 'g')
      assert.ok((text.match(symbol) || []).length >= 2, 'Rename fixture needs a declaration and a use')
      for (const [target, value] of result) assert.equal(value.text, documents.get(target).text.replace(symbol, 'editorRenamed'), 'Rename must update every declaration and use without other changes')
      await apply(result)
      features.rename = 'applied and diagnosed'
      await restore()
    } else features.rename = 'not advertised by backend'
    if (capabilities.documentFormattingProvider) {
      process.stderr.write(`Checking formatting edits: ${uri}\n`)
      await apply(new Map(documents).set(uri, { ...documents.get(uri), text: fixture.unformatted }))
      const params = { textDocument: { uri }, options: { tabSize: 2, insertSpaces: true } }
      const edits = await client.request('textDocument/formatting', params)
      const formatted = applyTextEdits(fixture.unformatted, edits)
      assert.notEqual(formatted, fixture.unformatted, 'Formatting must change the deliberately unformatted source')
      assert.equal(formatted.replace(/\s/g, ''), fixture.unformatted.replace(/\s/g, ''), 'Formatting must preserve fixture tokens')
      await apply(new Map(documents).set(uri, { ...documents.get(uri), text: formatted }))
      const second = await client.request('textDocument/formatting', params)
      assert.equal(applyTextEdits(formatted, second || []), formatted, 'Formatting must be stable on a second application')
      features.formatting = 'applied, stable and diagnosed'
      await restore()
    } else features.formatting = 'not advertised by backend'
    if (capabilities.codeActionProvider) {
      process.stderr.write(`Checking codeActions edits: ${uri}\n`)
      const after = client.messages.length
      change(uri, fixture.broken)
      const published = await diagnosticsSince(uri, after, diagnostics => diagnostics.some(item => item.severity === 1))
      const diagnostics = published.params.diagnostics
      const diagnostic = diagnostics.find(item => item.severity === 1)
      const actions = await client.request('textDocument/codeAction', { textDocument: { uri }, range: diagnostic.range, context: { diagnostics, only: ['quickfix'] } })
      assert.ok(Array.isArray(actions) && actions.length, 'Fixable diagnostic returned no code actions')
      let action = actions.find(item => !item.disabled && fixture.actionTitle.test(item.title))
      assert.ok(action, `Missing expected quick fix: ${JSON.stringify(actions)}`)
      if (!action.edit && capabilities.codeActionProvider.resolveProvider) action = await client.request('codeAction/resolve', action)
      const result = applyWorkspaceEdit(documents, action.edit)
      assert.match(result.get(uri).text, fixture.fixedText)
      await apply(result)
      features.codeActions = 'quick fix applied and diagnostic cleared'
      await restore()
    } else features.codeActions = 'not advertised by backend'
    return { features, version: documents.get(uri).version }
  } finally {
    await restore()
    for (const [target, source] of originals) assert.equal(readFileSync(fileURLToPath(target), 'utf8'), source, 'Editing qualification must not change disk sources')
    for (const [target, value] of documents) if (target !== uri && value.version != null) client.send({ method: 'textDocument/didClose', params: { textDocument: { uri: target } } })
  }
}
