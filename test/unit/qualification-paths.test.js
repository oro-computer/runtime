import test from 'node:test'
import assert from 'node:assert/strict'
import { existsSync, mkdtempSync, mkdirSync, realpathSync, symlinkSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { pathToFileURL } from 'node:url'
import { scopeGuard } from '../../src/cli/tooling/scope.cjs'

test('qualification models and document URIs agree under a symlinked temporary directory', () => {
  const directory = realpathSync.native(mkdtempSync(path.join(tmpdir(), 'oro qualification paths ')))
  const physical = path.join(directory, 'physical temp # space')
  const alias = path.join(directory, 'linked temp # space')
  mkdirSync(physical)
  symlinkSync(physical, alias, process.platform === 'win32' ? 'junction' : 'dir')
  const result = spawnSync(process.execPath, ['--input-type=module', '-e', `
    import assert from 'node:assert/strict'
    import { realpathSync, mkdirSync, writeFileSync } from 'node:fs'
    import path from 'node:path'
    import { pathToFileURL, fileURLToPath } from 'node:url'
    import { createQualificationDirectory } from ${JSON.stringify(new URL('../../bin/qualification-directory.js', import.meta.url).href)}
    import { modelFor } from ${JSON.stringify(new URL('../../src/cli/tooling/model.cjs', import.meta.url).href)}
    import { scopeGuard } from ${JSON.stringify(new URL('../../src/cli/tooling/scope.cjs', import.meta.url).href)}
    const physical = process.argv[1]
    for (const family of ['typescript', 'cpp', 'swift', 'kotlin', 'npm']) {
      const root = createQualificationDirectory('oro-' + family + '-fixture-')
      assert.equal(path.dirname(root), physical)
      const configuration = path.join(root, 'oro.toml')
      writeFileSync(configuration, '[build]\\noutput = "out with spaces"\\n')
      mkdirSync(path.join(root, 'sources'))
      const filename = path.join(root, 'sources', 'main.ts')
      writeFileSync(filename, 'export const value = 1\\n')
      const model = modelFor({ root, configuration, config: { build: { output: 'out with spaces' } }, env: {} }, {}, family, {
        originalSources: [realpathSync.native(filename)]
      })
      assert.deepEqual(model.originalSources, [filename])
      assert.ok(model.generation.startsWith(path.join(root, 'out with spaces', '.oro-tooling')))
      assert.equal(pathToFileURL(filename).href, pathToFileURL(model.originalSources[0]).href)
      assert.equal(fileURLToPath(pathToFileURL(root)), realpathSync.native(root))
      const uri = pathToFileURL(filename).href
      const scope = scopeGuard(model)
      let backendUri = uri.replace('main.ts', '%6dain.ts')
      if (process.platform === 'win32') backendUri = backendUri.replace(new RegExp('^file:///([a-z]):', 'i'), (_, drive) => 'file:///' + drive.toLowerCase() + '%3A')
      assert.ok(scope.accepts(backendUri), backendUri)
      // Definitions and workspace edits can target a declared source before
      // the editor opens it. Its URI must already have a stable spelling.
      assert.equal(scope.outgoing({ result: { targetUri: backendUri } }).result.targetUri, uri)
      scope.transform({ method: 'textDocument/didOpen', params: { textDocument: { uri } } })
      assert.equal(scope.outgoing({ params: { uri: backendUri } }).params.uri, uri)
      assert.equal(scope.outgoing({ result: [{ uri: backendUri }] }).result[0].uri, uri)
      const edits = { changes: { [backendUri]: [] } }
      assert.doesNotThrow(() => scope.validate({ result: edits }, 'textDocument/rename'))
      assert.deepEqual(Object.keys(scope.outgoing(edits).changes), [uri])
    }
    console.log('Canonical qualification paths and URIs passed')
  `, physical], {
    env: { ...process.env, TMPDIR: alias, TMP: alias, TEMP: alias },
    encoding: 'utf8',
    timeout: 10000
  })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.match(result.stdout, /Canonical qualification paths and URIs passed/)
})

test('Windows filesystem aliases preserve editor URIs for references and workspace edits', { skip: process.platform !== 'win32' }, () => {
  const root = realpathSync.native(mkdtempSync(path.join(tmpdir(), 'oro Windows aliases ')))
  const original = path.join(root, 'Original Directory')
  const alias = path.join(root, 'Editor Directory')
  mkdirSync(original)
  symlinkSync(original, alias, 'junction')
  const filename = path.join(original, 'Original Source # % ! & (copy) Ω.cc')
  writeFileSync(filename, 'int editorValue = 1;')
  // A junction keeps the editor path distinct even on volumes without 8.3
  // names. Pass paths as data to the native API, never as shell source.
  const script = `
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class OroShortPath {
  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
  public static extern uint GetShortPathNameW(string longPath, StringBuilder shortPath, uint bufferSize);
}
'@
$sourcePath = $env:ORO_TEST_SHORT_PATH_SOURCE
$length = [OroShortPath]::GetShortPathNameW($sourcePath, $null, 0)
if ($length -eq 0) { throw [System.ComponentModel.Win32Exception]::new([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()) }
$buffer = [System.Text.StringBuilder]::new([int]$length)
$length = [OroShortPath]::GetShortPathNameW($sourcePath, $buffer, [uint32]$buffer.Capacity)
if ($length -eq 0) { throw [System.ComponentModel.Win32Exception]::new([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()) }
if ($length -ge $buffer.Capacity) { throw 'GetShortPathNameW returned an incomplete path' }
[Console]::Out.Write($buffer.ToString())
`
  const result = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-EncodedCommand', Buffer.from(script, 'utf16le').toString('base64')], {
    env: { ...process.env, ORO_TEST_SHORT_PATH_SOURCE: path.join(alias, path.basename(filename)) },
    encoding: 'utf8',
    timeout: 30000
  })
  assert.ifError(result.error)
  assert.equal(result.status, 0, result.stdout + result.stderr)
  const short = result.stdout.trim()
  assert.equal(realpathSync.native(short), realpathSync.native(filename))
  const uri = pathToFileURL(short).href
  const canonicalUri = pathToFileURL(realpathSync.native(filename)).href
  assert.notEqual(uri, canonicalUri, 'The editor must open an alias of the original source')
  const backendUri = pathToFileURL(existsSync(filename.toLowerCase()) ? filename.toLowerCase() : filename).href
  const scope = scopeGuard({ language: 'cpp', projectRoot: root, originalSources: [filename] })
  const opened = { method: 'textDocument/didOpen', params: { textDocument: { uri } } }
  scope.transform(opened)
  assert.equal(opened.params.textDocument.uri, canonicalUri)
  assert.ok(scope.accepts(backendUri))
  assert.deepEqual(scope.outgoing({ result: [{ uri: backendUri }] }).result, [{ uri }])
  const edit = { changes: { [backendUri]: [] } }
  assert.doesNotThrow(() => scope.validate({ result: edit }, 'textDocument/rename'))
  assert.deepEqual(Object.keys(scope.outgoing(edit).changes), [uri])
})
