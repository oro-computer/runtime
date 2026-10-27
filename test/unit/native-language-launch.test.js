import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, readFileSync, realpathSync, symlinkSync } from 'node:fs'
import { createRequire } from 'node:module'
import { tmpdir } from 'node:os'
import { runInNewContext } from 'node:vm'
import path from 'node:path'
import { resolveProgram, stdioLaunch } from '../../src/cli/tooling/servers.cjs'
import { editorQualificationLanguages } from '../../bin/qualify-editor-install.js'
import { doctor } from '../../src/cli/tooling/doctor.cjs'

test('SourceKit preflight rejects Swift 5.10 and accepts the Swift 6.0 and 6.2 options', async () => {
  const filename = new URL('../../src/cli/tooling/servers.cjs', import.meta.url)
  const source = readFileSync(filename, 'utf8')
  const require = createRequire(filename)
  const helpByVersion = {
    '5.10': 'USAGE: sourcekit-lsp [<options>]\n  --sync\n  -index-store-path <index-store-path>\n  --help\n',
    '6.0': 'USAGE: sourcekit-lsp [<options>]\n  --default-workspace-type <default-workspace-type>\n  --compilation-db-search-path <compilation-db-search-path>\n  --help\n',
    6.2: 'USAGE: sourcekit-lsp [<options>] [<subcommand>]\n  --default-workspace-type <default-workspace-type>\n  --compilation-db-search-path <compilation-db-search-path>\n  --help\n'
  }
  for (const [version, help] of Object.entries(helpByVersion)) {
    const command = `/toolchains/swift-${version}/sourcekit-lsp`
    const environment = { DEVELOPER_DIR: '/selected/Xcode.app/Contents/Developer' }
    const module = { exports: {} }
    runInNewContext(source, {
      module,
      process,
      require: name => name === './process.cjs'
        ? {
            execute: async (executable, args, options) => {
              assert.equal(executable, command)
              assert.equal(args.join(' '), '--help')
              assert.equal(options.env, environment)
              assert.ok(options.timeout > 0 && options.timeout <= 10000)
              return help
            }
          }
        : require(name)
    })
    const checked = module.exports.preflightSourceKit(command, environment)
    if (version === '5.10') {
      await assert.rejects(checked, error => {
        assert.match(error.message, /Swift 6\.0 or later.*Xcode 16 or later/)
        assert.ok(error.message.includes(command))
        assert.match(error.message, /--default-workspace-type.*--compilation-db-search-path/)
        return true
      })
    } else await checked
  }
})

test('SourceKit preflight requires both workspace options and propagates execution failures', async () => {
  const filename = new URL('../../src/cli/tooling/servers.cjs', import.meta.url)
  const source = readFileSync(filename, 'utf8')
  const require = createRequire(filename)
  const failure = new Error('selected sourcekit-lsp failed: loader unavailable')
  for (const output of ['--default-workspace-type <type>\n', '--compilation-db-search-path <path>\n', failure]) {
    const module = { exports: {} }
    runInNewContext(source, {
      module,
      process,
      require: name => name === './process.cjs'
        ? {
            execute: async () => { if (output === failure) throw failure; return output }
          }
        : require(name)
    })
    await assert.rejects(module.exports.preflightSourceKit('/selected/sourcekit-lsp', {}), error => {
      if (output === failure) assert.equal(error, failure)
      else assert.match(error.message, /requires SourceKit-LSP from Swift 6\.0 or later/)
      return true
    })
  }
})

test('doctor rejects an incompatible SourceKit executable before inspecting its Swift model', async () => {
  const root = realpathSync.native(mkdtempSync(path.join(tmpdir(), 'oro sourcekit preflight ')))
  const project = { root, config: { language_server: { swift: { command: process.execPath } } }, env: process.env }
  let inspected = false
  const report = await doctor({ language: 'swift', sdk: root }, project, {
    swift: async () => { inspected = true; throw new Error('Unexpected Swift model inspection') }
  })
  assert.equal(report.ok, false)
  assert.equal(inspected, false)
  const server = report.checks.find(check => check.name === 'swift.server')
  assert.equal(server.status, 'error')
  assert.match(server.message, /Swift 6\.0 or later/)
  assert.ok(server.message.includes(realpathSync(process.execPath)))
  project.config.language_server.swift.adapter = 'stdio'
  const generic = await doctor({ language: 'swift', sdk: root }, project, {
    swift: async () => { inspected = true; throw new Error('Unexpected Swift model inspection') }
  })
  assert.equal(generic.ok, true)
  assert.equal(inspected, false)
  assert.match(generic.checks.find(check => check.name === 'swift.model').message, /Backend owns project discovery/)
})

test('Windows defers all C-family servers while preserving web editing and other hosts', () => {
  const filename = new URL('../../src/cli/tooling/servers.cjs', import.meta.url)
  const source = readFileSync(filename, 'utf8')
  const languages = ['c', 'cpp', 'c++', 'objc', 'objcpp', 'objective-c', 'objective-c++']
  for (const platform of ['win32', 'linux', 'darwin']) {
    const module = { exports: {} }
    runInNewContext(source, { module, require: createRequire(filename), process: { platform } })
    const { serverSettings } = module.exports
    for (const language of languages) {
      if (platform === 'win32') {
        assert.throws(() => serverSettings({}, language, '.'), /C\/C\+\+ language-server support on Windows is deferred/)
        assert.throws(() => serverSettings({ language_server: { cpp: { adapter: 'stdio', command: 'custom-server' } } }, language, '.', 'override'), /support on Windows is deferred/)
      } else {
        assert.equal(serverSettings({}, language, '.').adapter, 'clangd')
      }
    }
    for (const language of ['typescript', 'javascript', 'ts', 'js', 'tsx', 'jsx', 'web']) {
      assert.equal(serverSettings({}, language, '.').adapter, 'typescript')
    }
  }
})

test('release editor qualification retains Windows TypeScript and Linux/macOS native coverage', () => {
  assert.deepEqual(editorQualificationLanguages('win32'), ['typescript'])
  assert.deepEqual(editorQualificationLanguages('linux'), ['typescript', 'c', 'cpp'])
  assert.deepEqual(editorQualificationLanguages('darwin'), ['typescript', 'c', 'cpp', 'objc', 'objcpp', 'swift'])
})

test('SourceKit receives workspace-relative build settings and the prepared index', () => {
  const root = realpathSync.native(mkdtempSync(path.join(tmpdir(), 'oro sourcekit launch ')))
  const generation = path.join(root, 'out with spaces', 'generation')
  const model = { projectRoot: root, originalSources: [], generation, indexStorePath: path.join(generation, 'IndexStore'), indexDatabasePath: path.join(generation, 'IndexDatabase') }
  const settings = { command: 'sourcekit-lsp', language: 'swift', adapter: 'sourcekit', args: [] }
  const launch = stdioLaunch({ root, env: {} }, settings, model)
  assert.deepEqual(launch.args, ['--default-workspace-type', 'compilationDatabase', '--compilation-db-search-path', path.relative(root, generation)])
  const { initializationOptions } = launch.initialize({ params: { initializationOptions: { index: { maxCoresPercentageToUseForBackgroundIndexing: 0.5 } } } }).params
  assert.deepEqual(initializationOptions.compilationDatabase.searchPaths, [path.relative(root, generation)])
  assert.deepEqual(initializationOptions.index, { indexStorePath: model.indexStorePath, indexDatabasePath: model.indexDatabasePath, maxCoresPercentageToUseForBackgroundIndexing: 0.5 })
  assert.throws(() => stdioLaunch({ root }, { ...settings, args: ['--compilation-db-search-path=elsewhere'] }, model), /Oro owns/)
})

test('multicall compiler lookup can preserve the selected driver name', { skip: process.platform === 'win32' }, () => {
  const root = realpathSync.native(mkdtempSync(path.join(tmpdir(), 'oro compiler name ')))
  const directory = path.join(root, 'bin')
  mkdirSync(directory)
  const compiler = path.join(directory, 'swiftc')
  symlinkSync(process.execPath, compiler)
  assert.equal(resolveProgram('swiftc', { PATH: directory }, { preserveSymlinks: true }), compiler)
  assert.equal(resolveProgram('swiftc', { PATH: directory }), realpathSync(process.execPath))
})
