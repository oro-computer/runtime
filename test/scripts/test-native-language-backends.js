// Qualify installed native backends before building the Oro SDK.
import assert from 'node:assert/strict'
import { writeFileSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { createQualificationDirectory } from '../../bin/qualification-directory.js'
import { execute } from '../../src/cli/tooling/process.cjs'
import { resolveProgram } from '../../src/cli/tooling/servers.cjs'
import { LspClient, qualifyEditing, editingCapabilities } from './lsp-client.js'

const languages = process.argv.slice(2)
if (!languages.length || languages.some(language => !['c', 'cpp', 'objc', 'objcpp', 'swift'].includes(language))) throw new Error('Select c, cpp, objc, objcpp or swift backends to qualify')
if (process.platform === 'darwin') {
  // Application builds discover clang++ on PATH. Verify editor installation
  // leaves that compiler able to find the SDK without explicit model flags.
  const root = createQualificationDirectory('oro compiler # ')
  const source = path.join(root, 'main.mm')
  const executable = path.join(root, 'compiler-smoke')
  const compiler = resolveProgram(process.env.CXX || 'clang++', process.env, { preserveSymlinks: true })
  writeFileSync(source, `#import <Foundation/Foundation.h>
#include <resolv.h>
#include <string>
int main() {
  @autoreleasepool {
    const std::string value("oro");
    return [@"oro" isEqualToString:[NSString stringWithUTF8String:value.c_str()]] ? 0 : 1;
  }
}
`)
  console.log(`Checking macOS application compiler after editor tool installation: ${compiler}`)
  await execute(compiler, ['-std=c++20', source, '-framework', 'Foundation', '-lresolv', '-o', executable], { cwd: root, timeout: 60000, log: true })
  await execute(executable, [], { cwd: root, timeout: 10000, log: true })
  console.log('ok - macOS application compiler headers, frameworks and system libraries')
}
const backend = fileURLToPath(new URL('../fixtures/language-server/native-backend.cjs', import.meta.url))
for (const language of languages) {
  const root = createQualificationDirectory('oro backend # ')
  const swift = language === 'swift'
  const objc = ['objc', 'objcpp'].includes(language)
  const filename = path.join(root, { swift: 'main.swift', c: 'main.c', cpp: 'main.cc', objc: 'main.m', objcpp: 'main.mm' }[language])
  const uri = pathToFileURL(filename).href
  const source = swift
    ? '#if ORO_BACKEND_REQUIRED\nimport Foundation\nlet editorValue = Data()\nlet mirror = editorValue\n#else\n#error("Missing Oro build settings")\n#endif\n'
    : '#ifndef ORO_BACKEND_REQUIRED\n#error Missing Oro build settings\n#endif\n' + (objc ? '#import <Foundation/Foundation.h>\nNSObject *editorValue;\nNSObject *probe(void) { return editorValue; }\n' : 'int editorValue = 1;\nint probe(void) { return editorValue; }\n')
  const invalid = source + (swift ? 'let invalid: Int = "bad"\n' : 'int invalid = "bad";\n')
  writeFileSync(filename, source)
  writeFileSync(path.join(root, 'fixture.json'), JSON.stringify({
    build: { output: 'out with spaces' },
    tooling: { swift: { sources: ['main.swift'], compiler_flags: ['-D', 'ORO_BACKEND_REQUIRED'] } }
  }))
  const client = new LspClient(process.execPath, [backend, language, root])
  try {
    const initialized = await client.request('initialize', { rootUri: pathToFileURL(root).href, capabilities: editingCapabilities })
    client.send({ method: 'initialized', params: {} })
    client.send({ method: 'textDocument/didOpen', params: { textDocument: { uri, languageId: language, version: 1, text: invalid } } })
    await client.wait(message => message.method === 'textDocument/publishDiagnostics' && message.params.uri === uri && message.params.diagnostics.some(item => item.severity === 1))
    const after = client.messages.length
    client.send({ method: 'textDocument/didChange', params: { textDocument: { uri, version: 2 }, contentChanges: [{ text: source }] } })
    await client.wait(message => client.messages.indexOf(message) >= after && message.method === 'textDocument/publishDiagnostics' && message.params.uri === uri && !message.params.diagnostics.some(item => item.severity === 1))
    const before = source.slice(0, source.indexOf('editorValue') + 1).split('\n')
    const position = { line: before.length - 1, character: before.at(-1).length }
    const { features } = await qualifyEditing(client, initialized.capabilities, uri, source, position, [uri], {
      language,
      version: 2,
      symbol: 'editorValue',
      diagnosticSuffix: swift ? '\nlet invalid: Int = "bad"\n' : '\nint invalid = "bad";\n',
      unformatted: source + (swift ? 'func editorFormat( ) -> Int {return 1}\n' : 'int editorFormat( void ){return 1;}\n'),
      broken: swift ? source + 'let needsSemicolon = 1 let anotherValue = 2\n' : source + 'int needsSemicolon = 1\n',
      actionTitle: /[Ss]emicolon|[Ii]nsert.*;/,
      fixedText: swift ? /let needsSemicolon = 1;\s*let anotherValue/ : /int needsSemicolon = 1;/
    })
    assert.deepEqual(features, {
      completion: 'passed',
      references: 'passed',
      rename: 'applied and diagnosed',
      formatting: 'applied, stable and diagnosed',
      codeActions: 'quick fix applied and diagnostic cleared'
    })
    console.log(JSON.stringify({ language, features, project: root }))
  } catch (error) {
    console.error(client.context())
    throw error
  } finally { await client.close() }
}
