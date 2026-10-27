#!/usr/bin/env node

// Real backend qualification uses temporary original files and the selected SDK.
import assert from 'node:assert/strict'
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { pathToFileURL } from 'node:url'
import { execute } from '../../src/cli/tooling/process.cjs'
import { createQualificationDirectory } from '../../bin/qualification-directory.js'
import { LspClient, qualifyEditing, editingCapabilities } from './lsp-client.js'

const language = process.argv[2]
if (!['c', 'cpp', 'objc', 'objcpp', 'typescript', 'swift'].includes(language)) throw new Error('Select c, cpp, objc, objcpp, typescript or swift')
const inferred = process.argv.includes('--inferred')
if (inferred && language !== 'typescript') throw new Error('--inferred requires typescript')
const oroc = process.env.OROC || 'oroc'
const root = createQualificationDirectory(`oro-${language}-lsp-`)
const web = language === 'typescript'
const swift = language === 'swift'
const objc = ['objc', 'objcpp'].includes(language)
const family = web ? 'typescript' : swift ? 'swift' : 'cpp'
const directory = web ? 'web' : 'native'
const extension = { c: 'c', cpp: 'cc', objc: 'm', objcpp: 'mm', typescript: 'ts', swift: 'swift' }[language]
const filename = path.join(root, directory, `main.${extension}`)
const source = web
  ? 'import { randomBytes } from "oro:crypto"\nexport const editorValue = randomBytes(4)\nexport const mirror = editorValue\n'
  : swift
    ? 'import Foundation\nlet editorValue = Data()\nlet mirror = editorValue\n'
    : objc
      ? '#import <Foundation/Foundation.h>\nNSObject *editorValue;\nNSObject *probe(void) { return editorValue; }\n'
      : '#include <oro/extension.h>\nint editorValue = ORO_RUNTIME_EXTENSION_ABI_VERSION;\nint probe(void) { return editorValue; }\n'
const invalid = source + (web ? 'const invalid: number = "bad"\n' : swift ? 'let invalid: Int = "bad"\n' : 'int invalid = "bad";\n')
mkdirSync(path.dirname(filename), { recursive: true })
mkdirSync(path.join(root, 'historical'))
writeFileSync(filename, source)
writeFileSync(path.join(root, 'historical', path.basename(filename)), source)
let configuration = '[meta]\nversion = "1.0.0"\nbundle_identifier = "computer.oro.editorqualification"\n[build]\nname = "editor_qualification"\noutput = "out with spaces"\n'
if (web) {
  if (!inferred) writeFileSync(path.join(root, 'tsconfig.json'), JSON.stringify({ compilerOptions: { target: 'ES2022', module: 'ESNext', moduleResolution: 'Bundler', noEmit: true, skipLibCheck: true }, include: ['web/**/*.ts'] }))
} else if (swift) configuration += '[tooling.swift]\nsources = ["native/main.swift"]\n'
else configuration += `[build.extensions]\nprobe = "native/main.${extension}"\n`
writeFileSync(path.join(root, 'oro.toml'), configuration)
process.stderr.write(`Preparing ${language} editing fixture\n`)
const model = JSON.parse(await execute(oroc, ['tooling', family, '--prepare', '--offline', root]))
assert.deepEqual(model.originalSources, inferred ? [] : [filename])
if (web) assert.equal(model.sourceMode, inferred ? 'inferred' : 'configured')
assert.ok(model.generation.startsWith(path.join(root, 'out with spaces', '.oro-tooling')))
const client = new LspClient(oroc, ['language-server', '--language', language, '--offline'], { cwd: tmpdir() })
const uri = pathToFileURL(filename).href
const position = (text, token) => {
  const offset = text.indexOf(token) + 1
  assert.ok(offset > 0, token)
  const lines = text.slice(0, offset).split('\n')
  return { line: lines.length - 1, character: lines.at(-1).length }
}
try {
  process.stderr.write(`Initializing ${language} editing fixture\n`)
  const initial = await client.request('initialize', { processId: process.pid, rootUri: pathToFileURL(root).href, capabilities: editingCapabilities })
  process.stderr.write(`Diagnosing ${language} editing fixture\n`)
  client.send({ method: 'initialized', params: {} })
  client.send({ method: 'textDocument/didOpen', params: { textDocument: { uri, languageId: language, version: 1, text: invalid } } })
  await client.wait(message => message.method === 'textDocument/publishDiagnostics' && message.params.uri === uri && message.params.diagnostics.some(diagnostic => diagnostic.severity === 1))
  const after = client.messages.length
  client.send({ method: 'textDocument/didChange', params: { textDocument: { uri, version: 2 }, contentChanges: [{ text: source }] } })
  await client.wait(message => client.messages.indexOf(message) >= after && message.method === 'textDocument/publishDiagnostics' && message.params.uri === uri && !message.params.diagnostics.some(diagnostic => diagnostic.severity === 1))
  const token = web ? 'randomBytes(4)' : swift ? 'Data()' : objc ? 'NSObject' : 'ORO_RUNTIME_EXTENSION_ABI_VERSION'
  const hover = await client.request('textDocument/hover', { textDocument: { uri }, position: position(source, token) })
  assert.match(JSON.stringify(hover), web ? /randomBytes.*Buffer/s : swift ? /Data/ : objc ? /NSObject/ : /ORO_RUNTIME_EXTENSION_ABI_VERSION/)
  const editing = await qualifyEditing(client, initial.capabilities, uri, source, position(source, 'editorValue'), [uri], {
    language,
    version: 2,
    diagnosticSuffix: web ? '\nconst oroEditingInvalid: number = "bad"\n' : swift ? '\nlet oroEditingInvalid: Int = "bad"\n' : '\nint oroEditingInvalid = "bad";\n',
    symbol: 'editorValue',
    unformatted: source + (web ? 'export function editorFormat( ){return 1}\n' : swift ? 'func editorFormat( ) -> Int {return 1}\n' : 'int editorFormat( void ){return 1;}\n'),
    broken: web ? source.replace('= editorValue', '= editorValu') : swift ? source + 'let needsSemicolon = 1 let anotherValue = 2\n' : source + 'int needsSemicolon = 1\n',
    actionTitle: web ? /editorValue/ : /[Ss]emicolon|[Ii]nsert.*;/,
    fixedText: web ? /= editorValue\b/ : swift ? /let needsSemicolon = 1;\s*let anotherValue/ : /int needsSemicolon = 1;/
  })
  const { features } = editing
  let version = editing.version
  if (web && !inferred) {
    client.send({ method: 'textDocument/didChange', params: { textDocument: { uri, version: ++version }, contentChanges: [{ text: invalid }] } })
    const start = client.messages.length
    writeFileSync(path.join(root, 'oro.toml'), configuration + '\n# Refresh with an unsaved editor buffer.\n')
    const refreshed = await client.wait(message => client.messages.indexOf(message) >= start && message.method === 'window/logMessage' && message.params.message === 'Editor model ready', 120000)
    const ready = client.messages.indexOf(refreshed)
    await client.wait(message => client.messages.indexOf(message) > ready && message.method === 'textDocument/publishDiagnostics' && message.params.uri === uri && message.params.diagnostics.some(diagnostic => diagnostic.severity === 1))
    assert.equal(readFileSync(filename, 'utf8'), source, 'Refresh must preserve the on-disk application source')
    client.send({ method: 'textDocument/didChange', params: { textDocument: { uri, version: ++version }, contentChanges: [{ text: source }] } })
    const after = client.messages.length
    await client.wait(message => client.messages.indexOf(message) >= after && message.method === 'textDocument/publishDiagnostics' && message.params.uri === uri && !message.params.diagnostics.some(diagnostic => diagnostic.severity === 1))
    features.refreshWithUnsavedBuffer = 'passed'
  }
  assert.ok(!client.messages.some(message => message.method === 'textDocument/publishDiagnostics' && message.params.uri.includes('/historical/')))
  console.log(JSON.stringify({ language, sourceMode: model.sourceMode, platform: model.platform, diagnostics: 'passed', hover: 'passed', features, project: root }))
} catch (error) {
  console.error(client.context())
  throw error
} finally { await client.close() }
