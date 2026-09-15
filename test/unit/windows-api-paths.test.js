import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import path from 'node:path'
import vm from 'node:vm'

test('Windows File adapters preserve the basename, MIME type and writable file handle', async () => {
  const source = readFileSync(new URL('../../api/fs/web.js', import.meta.url), 'utf8')
    .replace(/^import .*\n/gm, '')
    .replace(/^export default /m, 'const exports = ')
    .replace(/^export /gm, '')
  const lookups = []
  const renames = []
  const context = vm.createContext({
    TextDecoder,
    WritableStream,
    URL,
    console: { warn () {} },
    DEFAULT_STREAM_HIGH_WATER_MARK: 65536,
    NotAllowedError: Error,
    path: path.win32,
    fs: {
      stat: async () => ({ size: 8, mtimeMs: 0 }),
      rename: async (...args) => { renames.push(args) }
    },
    mime: {
      lookup: async value => {
        lookups.push(value)
        return value === 'bin' ? [{ mime: 'application/octet-stream' }] : []
      }
    }
  })
  const api = vm.runInContext(`${source}; ({ createFile, createFileSystemFileHandle })`, context)
  const filename = String.raw`C:\Users\runner\Oro tests\data.bin`
  const file = await api.createFile(filename)
  assert.equal(file.name, 'data.bin')
  assert.equal(file.type, 'application/octet-stream')
  assert.deepEqual(lookups, ['bin'])
  const handle = await api.createFileSystemFileHandle(file)
  assert.equal(handle.name, 'data.bin')
  const writable = await api.createFileSystemFileHandle(filename)
  await writable.move('renamed.bin')
  assert.deepEqual(renames, [[filename, String.raw`C:\Users\runner\Oro tests\renamed.bin`]])
  const remote = await api.createFileSystemFileHandle('https://example.com/data.bin')
  await assert.rejects(remote.move('renamed.bin'), /readonly/)
})

test('CommonJS dirname preserves URL separators on Windows and supports native paths', () => {
  const source = readFileSync(new URL('../../api/commonjs/module.js', import.meta.url), 'utf8')
  const start = source.indexOf('export class JavaScriptModuleLoader ')
  const end = source.indexOf('export class JSONModuleLoader ', start)
  for (const [filename, dirname] of [
    ['oro://computer.oro.runtime.tests/fixtures/module.cjs', 'oro://computer.oro.runtime.tests/fixtures'],
    ['https://computer.oro.runtime.tests/module.cjs', 'https://computer.oro.runtime.tests'],
    [String.raw`C:\Oro tests\module.cjs`, String.raw`C:\Oro tests`]
  ]) {
    let received
    const context = vm.createContext({
      ModuleLoader: class {},
      Module: { compile: () => (...args) => { received = args } },
      path: path.win32,
      URL,
      process
    })
    const loader = vm.runInContext(`${source.slice(start, end).replace('export ', '')}; new JavaScriptModuleLoader()`, context)
    const module = {
      id: filename,
      scope: { exports: {} },
      createRequire: () => () => {},
      loader: { load: () => ({ text: '', id: filename }) }
    }
    assert.equal(loader.load(module), true)
    assert.equal(received[3], filename)
    assert.equal(received[4], dirname)
  }
})
