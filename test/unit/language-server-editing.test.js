import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { pathToFileURL } from 'node:url'
import { applyTextEdits, applyWorkspaceEdit, qualifyEditing } from '../scripts/lsp-client.js'
import { scopeGuard } from '../../src/cli/tooling/scope.cjs'

const edit = (line, start, end, newText) => ({ range: { start: { line, character: start }, end: { line, character: end } }, newText })

test('editing oracle applies UTF-16 ranges, CRLF and ordered insertions', () => {
  assert.equal(applyTextEdits('😀 old\r\nlast\n', [edit(1, 0, 4, 'end'), edit(0, 3, 6, 'new')]), '😀 new\r\nend\n')
  assert.equal(applyTextEdits('abc', [edit(0, 1, 1, 'X'), edit(0, 1, 1, 'Y'), edit(0, 1, 2, 'B')]), 'aXYBc')
  assert.throws(() => applyTextEdits('abc', [edit(0, 0, 2, ''), edit(0, 1, 3, '')]), /Overlapping/)
  assert.throws(() => applyTextEdits('abc', [edit(0, 0, 4, '')]), /outside/)
})

test('workspace edits preserve originals, check versions and reject unrelated files and resource operations', () => {
  const uri = 'file:///application/main.ts'
  const documents = new Map([[uri, { text: 'original', version: 7 }]])
  const changes = { changes: { [uri]: [edit(0, 0, 8, 'updated')] } }
  assert.equal(applyWorkspaceEdit(documents, changes).get(uri).text, 'updated')
  assert.equal(documents.get(uri).text, 'original')
  const versioned = { documentChanges: [{ textDocument: { uri, version: 6 }, edits: [] }] }
  assert.throws(() => applyWorkspaceEdit(documents, versioned), /outdated/)
  assert.throws(() => applyWorkspaceEdit(documents, { changes: { 'file:///generated/copy.ts': [] } }), /unrelated/)
  assert.throws(() => applyWorkspaceEdit(documents, { documentChanges: [{ kind: 'delete', uri }] }), /resource operation/)
})

test('qualification rejects empty formatting, ineffective quick fixes and incomplete renames', async () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro-edit-oracle-'))
  const file = path.join(root, 'main.ts')
  const uri = pathToFileURL(file).href
  const text = 'const editorValue = 1\nconst mirror = editorValue\n'
  writeFileSync(file, text)
  const fixture = { language: 'typescript', version: 1, symbol: 'editorValue', diagnosticSuffix: '\noroEditingInvalid\n', unformatted: text + 'function f( ){return 1}\n', broken: text.replace('= editorValue', '= editorValu'), actionTitle: /Fix/, fixedText: /editorValue/ }
  for (const [capabilities, response, expected] of [
    [{ documentFormattingProvider: true }, [], /Formatting must change/],
    [{ codeActionProvider: true }, [], /no code actions/],
    [{ codeActionProvider: true }, [{ title: 'Fix spelling', edit: { changes: { [uri]: [] } } }], /no effective edits/],
    [{ renameProvider: true }, { changes: { [uri]: [edit(0, 6, 17, 'editorRenamed')] } }, /every declaration and use/]
  ]) {
    const client = {
      messages: [],
      request: async () => response,
      send (message) {
        const text = message.params.contentChanges[0].text
        this.messages.push({ method: 'textDocument/publishDiagnostics', params: { uri, version: message.params.textDocument.version, diagnostics: (text.includes('= editorValu\n') || text.includes('oroEditingInvalid')) ? [{ severity: 1, range: edit(1, 15, 25, '').range }] : [] } })
      },
      async wait (predicate) { const message = this.messages.find(predicate); assert.ok(message, 'Expected fresh diagnostics'); return message }
    }
    await assert.rejects(qualifyEditing(client, capabilities, uri, text, { line: 0, character: 7 }, [uri], fixture), expected)
  }
})

test('code-action validation checks protocol edits without interpreting opaque TypeScript metadata', () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro-action-scope-'))
  const filename = path.join(root, 'main.ts')
  writeFileSync(filename, 'const value = 1')
  const uri = pathToFileURL(filename).href
  const scope = scopeGuard({ projectRoot: root, originalSources: [filename], language: 'typescript' })
  const action = { title: 'Fix spelling', edit: { changes: { [uri]: [] } }, data: { changes: [{ fileName: filename, textChanges: [] }] } }
  assert.doesNotThrow(() => scope.validate({ result: [action] }, 'textDocument/codeAction'))
  assert.doesNotThrow(() => scope.validate({ result: action }, 'codeAction/resolve'))
  action.edit.changes = { 'file:///generated/copy.ts': [] }
  assert.throws(() => scope.validate({ result: [action] }, 'textDocument/codeAction'), /outside the declared/)
  assert.throws(() => scope.validate({ result: action }, 'codeAction/resolve'), /outside the declared/)
  assert.throws(() => scope.validate({ method: 'workspace/applyEdit', params: { edit: action.edit } }), /outside the declared/)
})
