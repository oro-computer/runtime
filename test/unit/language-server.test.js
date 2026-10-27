import test from 'node:test'
import assert from 'node:assert/strict'
import { once } from 'node:events'
import { execFileSync } from 'node:child_process'
import { existsSync, mkdirSync, mkdtempSync, readFileSync, realpathSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { PassThrough } from 'node:stream'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { Messages, encode, initializeClient } from '../../src/cli/tooling/lsp.cjs'
import { findConfiguration } from '../../src/cli/tooling/project.cjs'
import { serverSettings } from '../../src/cli/tooling/servers.cjs'
import { main } from '../../src/cli/tooling/main.cjs'
import { modelFor, publish, freshness, words } from '../../src/cli/tooling/model.cjs'
import { nativeModel } from '../../src/cli/tooling/native.cjs'
import { typescriptModel } from '../../src/cli/tooling/typescript.cjs'
import initializeTypeScriptPlugin from '../../src/cli/tooling/typescript-plugin.cjs'
import { swiftModel } from '../../src/cli/tooling/swift.cjs'
import { doctor } from '../../src/cli/tooling/doctor.cjs'
import path from 'node:path'

test('language-server requires language selection before reading project inputs', async () => {
  await assert.rejects(main(['language-server']), /--language is required/)
  await assert.rejects(main(['language-server', '--config', '/missing/oro.toml', '--prepare']), /--language is required/)
  await assert.rejects(main(['language-server', '--language']), /--language requires a value/)
})

test('editor initialization selects the Oro root independently of the launch directory', () => {
  const root = fileURLToPath(new URL('../fixtures/language-server', import.meta.url))
  const configuration = realpathSync(path.join(root, 'oro.toml'))
  const source = path.join(root, 'android')
  const cwd = path.parse(root).root
  const uri = pathToFileURL(source).href
  assert.equal(findConfiguration({}, { workspaceFolders: [{ uri, name: 'Android' }], rootUri: pathToFileURL(cwd).href }, cwd), configuration)
  assert.equal(findConfiguration({}, { workspaceFolders: null, rootUri: uri }, cwd), configuration)
  assert.equal(findConfiguration({}, { workspaceFolders: [], rootUri: null, rootPath: source }, cwd), configuration)
  assert.equal(findConfiguration({}, {}, source), configuration)
  assert.equal(findConfiguration({ project: root }, { rootUri: pathToFileURL(cwd).href }, cwd), configuration)
  assert.equal(findConfiguration({ config: configuration }, { rootUri: 'untitled:buffer' }, cwd), configuration)
  assert.throws(() => findConfiguration({ project: source }, {}, cwd), /selected project directory/)
  assert.throws(() => findConfiguration({}, { rootUri: 'untitled:buffer' }, cwd), /local file URI/)
})

test('Coc client roots select a nested application when workspace folders have no project', () => {
  const workspace = mkdtempSync(path.join(tmpdir(), 'oro coc roots-'))
  const application = path.join(workspace, 'test #1')
  const missing = path.join(workspace, 'missing')
  for (const directory of [application, missing]) mkdirSync(directory)
  writeFileSync(path.join(application, 'oro.toml'), '')
  const configuration = realpathSync(path.join(application, 'oro.toml'))
  const workspaceFolders = [{ uri: pathToFileURL(workspace).href, name: 'Repository' }]
  const rootUri = pathToFileURL(application).href
  assert.equal(findConfiguration({}, { workspaceFolders, rootUri, rootPath: missing }, workspace), configuration)
  assert.equal(findConfiguration({}, { workspaceFolders, rootPath: application }, workspace), configuration)
  // A broad workspace alone cannot select a child application or use cwd instead.
  assert.throws(() => findConfiguration({}, { workspaceFolders }, application), /No oro.toml or oro.ini found/)
  assert.throws(() => findConfiguration({}, { workspaceFolders, rootUri: pathToFileURL(missing).href, rootPath: application }, application), error => {
    assert.ok(error.message.includes(`in or above ${workspace}, ${missing}`))
    return true
  })
  // A client root must not bypass ambiguity between actual workspace projects.
  const other = path.join(workspace, 'other')
  mkdirSync(other)
  writeFileSync(path.join(other, 'oro.ini'), '')
  assert.throws(() => findConfiguration({}, {
    workspaceFolders: [
      { uri: rootUri, name: 'Application' },
      { uri: pathToFileURL(other).href, name: 'Other' }
    ],
    rootUri
  }, workspace), /multiple Oro projects.*--config/)
})

test('root discovery handles encoded paths, legacy markers and ambiguous workspaces', () => {
  const workspace = mkdtempSync(path.join(tmpdir(), 'oro project roots-'))
  const modern = path.join(workspace, 'application #1')
  const legacy = path.join(workspace, 'legacy')
  const missing = path.join(workspace, 'missing')
  for (const directory of [modern, legacy, missing]) mkdirSync(directory)
  writeFileSync(path.join(modern, 'oro.ini'), '')
  writeFileSync(path.join(modern, 'oro.toml'), '')
  writeFileSync(path.join(legacy, 'oro.ini'), '')
  const modernConfiguration = realpathSync(path.join(modern, 'oro.toml'))
  const modernFolder = { uri: pathToFileURL(modern).href, name: 'Modern' }
  const legacyFolder = { uri: pathToFileURL(legacy).href, name: 'Legacy' }
  assert.equal(findConfiguration({}, { rootUri: modernFolder.uri }, workspace), modernConfiguration)
  assert.equal(findConfiguration({}, { rootUri: legacyFolder.uri }, workspace), realpathSync(path.join(legacy, 'oro.ini')))
  assert.equal(findConfiguration({}, { workspaceFolders: [modernFolder, modernFolder] }, workspace), modernConfiguration)
  assert.throws(() => findConfiguration({}, { workspaceFolders: [modernFolder, legacyFolder] }, workspace), /multiple Oro projects.*--config/)
  assert.equal(findConfiguration({ config: './application #1/oro.toml' }, { workspaceFolders: [modernFolder, legacyFolder] }, workspace), modernConfiguration)
  // A supplied editor root must not fall back to an unrelated project in cwd.
  assert.throws(() => findConfiguration({}, { rootUri: pathToFileURL(missing).href }, modern), error => {
    assert.ok(error.message.includes(`in or above ${missing}`))
    assert.ok(error.message.includes(`launch directory: ${modern}`))
    assert.match(error.message, /rootPatterns.*--config/)
    return true
  })
})

test('initialization discovery preserves fragmented and coalesced frames during startup', async () => {
  const source = new PassThrough()
  const initialized = initializeClient(source)
  const request = { jsonrpc: '2.0', id: 1, method: 'initialize', params: { rootUri: 'file:///project%20name' } }
  const notification = { jsonrpc: '2.0', method: 'initialized', params: {} }
  const later = { jsonrpc: '2.0', method: 'textDocument/didOpen', params: { text: 'こんにちは' } }
  const first = encode(request)
  const last = encode(later)
  source.write(first.subarray(0, 19))
  source.write(Buffer.concat([first.subarray(19), encode(notification), last.subarray(0, 24)]))
  const client = await initialized
  try {
    assert.deepEqual(client.message, request)
    // Finish another frame before a backend is attached, as during Gradle setup.
    source.end(last.subarray(24))
    const output = []
    const ended = once(client.input, 'end')
    client.input.on('data', chunk => output.push(chunk))
    await ended
    assert.deepEqual(Buffer.concat(output), Buffer.concat([first, encode(notification), last]))
  } finally {
    client.close()
  }
})

test('initialization discovery rejects malformed requests and disconnected clients', async () => {
  for (const content of [
    Buffer.alloc(0),
    Buffer.from('Content-Length: 100\r\n\r\n{}'),
    encode({ jsonrpc: '2.0', method: 'initialized', params: {} })
  ]) {
    const source = new PassThrough()
    const initialized = initializeClient(source)
    source.end(content)
    await assert.rejects(initialized, /initialize request|Truncated LSP/)
    assert.equal(source.listenerCount('error'), 0)
  }
})

test('language families select installed defaults without Android setup', () => {
  const root = path.resolve('.')
  for (const language of ['typescript', 'javascript', 'typescriptreact', 'web', 'tsx']) {
    const settings = serverSettings({}, language, root)
    assert.equal(settings.command, 'typescript-language-server')
    assert.deepEqual(settings.args, ['--stdio'])
    assert.equal(settings.adapter, 'typescript')
  }
  for (const language of ['c', 'cpp', 'c++', 'objc', 'objcpp', 'objective-c++']) {
    const settings = serverSettings({}, language, root)
    assert.equal(settings.command, 'clangd')
    assert.deepEqual(settings.args, [])
    assert.equal(settings.adapter, 'clangd')
  }
  assert.equal(serverSettings({}, 'kotlin', root).command, 'kotlin-language-server')
  assert.equal(serverSettings({}, 'swift', root).command, 'sourcekit-lsp')
})

function editorProject () {
  const root = realpathSync(mkdtempSync(path.join(tmpdir(), 'oro editor models-')))
  const configuration = path.join(root, 'oro.toml')
  writeFileSync(configuration, '[build]\noutput = "out with spaces"\n')
  return { root, configuration, config: { build: { output: 'out with spaces' } }, env: { ...process.env }, oroc: process.execPath }
}

test('editor generations preserve custom output paths and refresh identity', () => {
  const project = editorProject()
  const identity = { platform: 'linux', arch: 'x86_64', buildType: 'debug' }
  const model = modelFor(project, {}, 'cpp', identity)
  assert.equal(existsSync(path.dirname(model.pointer)), false)
  assert.equal(freshness(model).status, 'not-prepared')
  publish({ ...model, artifacts: [] })
  assert.equal(freshness(model).status, 'current')
  const refreshed = publish({ ...modelFor(project, { refresh: true }, 'cpp', identity), artifacts: [] })
  assert.notEqual(refreshed.generation, model.generation)
  assert.equal(modelFor(project, {}, 'cpp', identity).generation, refreshed.generation)
  assert.notEqual(modelFor(project, {}, 'cpp', { ...identity, buildType: 'release' }).pointer, model.pointer)
  writeFileSync(project.configuration, '[build]\noutput = "out with spaces"\nname = "changed"\n')
  assert.equal(freshness(modelFor(project, {}, 'cpp', identity)).status, 'stale')
})

test('native database export keeps literal argv, original identity and declared scope', async () => {
  const project = editorProject()
  const source = path.join(project.root, 'original file.cc')
  const historical = path.join(project.root, 'historical.cc')
  writeFileSync(source, 'int current;\n')
  writeFileSync(historical, 'int current;\n')
  const database = path.join(project.root, 'compile_commands.json')
  const args = ['clang++', '-I', 'include with spaces', '-DVALUE=a b', '-c', source]
  writeFileSync(database, JSON.stringify([
    { directory: project.root, file: source, arguments: args },
    { directory: project.root, file: historical, arguments: ['clang++', '-c', historical] }
  ]))
  project.config.tooling = { cpp: { compilation_database: database, sources: ['original file.cc'] } }
  const before = readFileSync(database, 'utf8')
  const inspected = await nativeModel({ inspect: true }, project)
  assert.equal(existsSync(inspected.generation), false)
  const model = await nativeModel({}, project)
  assert.deepEqual(model.originalSources, [source])
  assert.deepEqual(JSON.parse(readFileSync(model.compileCommands, 'utf8'))[0].arguments, args)
  assert.equal(readFileSync(database, 'utf8'), before)
  assert.deepEqual(words('["a b","","$literal"]'), ['a b', '', '$literal'])
  assert.throws(() => words('$(shell)'), /literal/)
})

test('TypeScript model follows config inheritance and never rewrites application inputs', async () => {
  const project = editorProject()
  const sdk = path.join(project.root, 'sdk')
  mkdirSync(path.join(sdk, 'api'), { recursive: true })
  writeFileSync(path.join(sdk, 'api', 'index.d.ts'), 'declare module "oro:fixture" { export const value: number }\n')
  const source = path.join(project.root, 'main.ts')
  writeFileSync(source, 'import { value } from "oro:fixture"\nexport const result = value\n')
  writeFileSync(path.join(project.root, 'historical.ts'), 'syntax error from a historical snapshot\n')
  const original = '{"extends":"./base.json","files":["main.ts"]}'
  writeFileSync(path.join(project.root, 'tsconfig.json'), original)
  writeFileSync(path.join(project.root, 'base.json'), '{"compilerOptions":{"strict":true,"noEmit":true}}')
  project.config.tooling = { typescript: { typescript: fileURLToPath(new URL('../../node_modules/typescript', import.meta.url)) } }
  const model = await typescriptModel({ sdk, prepare: true }, project)
  assert.deepEqual(model.originalSources, [source])
  assert.equal(readFileSync(path.join(project.root, 'tsconfig.json'), 'utf8'), original)
  assert.equal(model.typings, path.join(sdk, 'api', 'index.d.ts'))
  assert.ok(model.freshnessInputs.includes(path.join(project.root, 'base.json')))
  const generated = JSON.parse(readFileSync(model.editorConfig, 'utf8'))
  assert.equal(generated.extends, path.join(project.root, 'tsconfig.json'))
  assert.deepEqual(generated.files, [source, model.typings])
  writeFileSync(path.join(project.root, 'base.json'), '{"compilerOptions":{"strict":false,"noEmit":true}}')
  assert.equal(freshness(await typescriptModel({ sdk, inspect: true }, project)).status, 'stale')
})

test('web projects with build scripts use inferred projects until a compiler config is supplied', async () => {
  const project = editorProject()
  const sdk = path.join(project.root, 'sdk')
  mkdirSync(path.join(sdk, 'api'), { recursive: true })
  writeFileSync(path.join(sdk, 'api', 'index.d.ts'), 'declare module "oro:fixture" { export const value: number }\n')
  mkdirSync(path.join(project.root, 'src'))
  mkdirSync(path.join(project.root, 'historical'))
  writeFileSync(path.join(project.root, 'src', 'main.jsx'), 'export const view = <div />\n')
  writeFileSync(path.join(project.root, 'historical', 'main.jsx'), 'export const view = <div />\n')
  writeFileSync(path.join(project.root, 'build.js'), 'throw new Error("Editor discovery must not execute the application build")\n')
  project.config.build.script = 'node build.js'
  project.config['build_copy-map_src/images'] = 'images'
  project.config.tooling = { typescript: { typescript: fileURLToPath(new URL('../../node_modules/typescript', import.meta.url)) } }
  const model = await typescriptModel({ sdk }, project)
  assert.equal(model.sourceMode, 'inferred')
  assert.deepEqual(model.originalSources, [])
  assert.equal(model.tsconfig, null)
  assert.equal(existsSync(path.join(project.root, 'tsconfig.json')), false)
  assert.equal(existsSync(path.join(project.root, 'jsconfig.json')), false)
  assert.ok(model.freshnessInputs.includes(path.join(project.root, 'jsconfig.json')))
  assert.deepEqual(JSON.parse(readFileSync(model.editorConfig, 'utf8')).files, [model.typings])
  writeFileSync(path.join(project.root, 'jsconfig.json'), '{"compilerOptions":{"allowJs":true,"jsx":"react-jsx"},"include":["src/**/*.jsx"]}')
  const configured = await typescriptModel({ sdk, inspect: true }, project)
  assert.equal(configured.sourceMode, 'configured')
  assert.deepEqual(configured.originalSources, [path.join(project.root, 'src', 'main.jsx')])
  assert.equal(freshness(configured).status, 'stale')
})

test('invalid explicit web scopes do not silently fall back to inferred projects', async () => {
  const project = editorProject()
  const sdk = path.join(project.root, 'sdk')
  mkdirSync(path.join(sdk, 'api'), { recursive: true })
  writeFileSync(path.join(sdk, 'api', 'index.d.ts'), '')
  project.config.tooling = { typescript: { typescript: fileURLToPath(new URL('../../node_modules/typescript', import.meta.url)), sources: ['missing'] } }
  await assert.rejects(typescriptModel({ sdk }, project), /Declared web source is missing/)
  project.config.tooling.typescript.sources = []
  writeFileSync(path.join(project.root, 'tsconfig.json'), '{ invalid json')
  await assert.rejects(typescriptModel({ sdk }, project), /tsconfig\.json/)
})

test('inferred semantic projects resolve Oro declarations without altering syntax hosts', async () => {
  const { default: ts } = await import('typescript')
  const project = editorProject()
  const source = path.join(project.root, 'main.ts')
  const typings = path.join(project.root, 'oro.d.ts')
  writeFileSync(source, 'import { value } from "oro:fixture"\nexport const result = value\n')
  writeFileSync(typings, 'declare module "oro:fixture" { export const value: number }\n')
  const modelFile = path.join(project.root, 'model.json')
  writeFileSync(modelFile, JSON.stringify({ schemaVersion: 1, language: 'typescript', sourceMode: 'inferred', typings }))
  const previous = process.env.ORO_TYPESCRIPT_MODEL
  process.env.ORO_TYPESCRIPT_MODEL = modelFile
  let plugin
  try { plugin = initializeTypeScriptPlugin({ typescript: ts }) } finally {
    if (previous === undefined) delete process.env.ORO_TYPESCRIPT_MODEL
    else process.env.ORO_TYPESCRIPT_MODEL = previous
  }
  const host = {
    ...ts.sys,
    useCaseSensitiveFileNames: () => ts.sys.useCaseSensitiveFileNames,
    getScriptFileNames: () => [source],
    getScriptVersion: () => '1',
    getScriptSnapshot: file => {
      const contents = ts.sys.readFile(file)
      return contents === undefined ? undefined : ts.ScriptSnapshot.fromString(contents)
    },
    getCompilationSettings: () => ({ noEmit: true, target: ts.ScriptTarget.ESNext }),
    getCurrentDirectory: () => project.root,
    getDefaultLibFileName: options => ts.getDefaultLibFilePath(options)
  }
  const snapshot = host.getScriptSnapshot
  const version = host.getScriptVersion
  const syntaxHost = { ...host }
  const syntaxFiles = syntaxHost.getScriptFileNames
  plugin.create({ project: { projectKind: ts.server.ProjectKind.Inferred, projectService: { serverMode: ts.LanguageServiceMode.PartialSemantic } }, languageServiceHost: syntaxHost, languageService: {} })
  assert.equal(syntaxHost.getScriptFileNames, syntaxFiles)
  const service = ts.createLanguageService(host)
  try {
    plugin.create({ project: { projectKind: ts.server.ProjectKind.Inferred, projectService: { serverMode: ts.LanguageServiceMode.Semantic } }, languageServiceHost: host, languageService: service })
    assert.equal(host.getScriptSnapshot, snapshot)
    assert.equal(host.getScriptVersion, version)
    assert.deepEqual(service.getSemanticDiagnostics(source), [])
    const info = service.getQuickInfoAtPosition(source, readFileSync(source, 'utf8').lastIndexOf('value'))
    assert.match(ts.displayPartsToString(info.displayParts), /value: number/)
  } finally { service.dispose() }
})

test('Swift database export preserves original commands without replaying a build', async () => {
  const project = editorProject()
  const source = path.join(project.root, 'Bridge.swift')
  writeFileSync(source, 'public struct Bridge {}\n')
  const commands = [{ directory: project.root, file: source, arguments: ['swiftc', '-module-name', 'Bridge', source] }]
  writeFileSync(path.join(project.root, 'swift-commands.json'), JSON.stringify(commands))
  project.config.tooling = { swift: { compilation_database: 'swift-commands.json' } }
  const model = await swiftModel({}, project)
  assert.deepEqual(model.originalSources, [source])
  assert.deepEqual(JSON.parse(readFileSync(model.compileCommands, 'utf8')), commands)
  await assert.rejects(swiftModel({ prepare: true }, project), /belongs to its build system/)
})

test('doctor reports missing tools and inspects models without preparing them', async () => {
  const project = editorProject()
  project.config.language_server = { cpp: { command: 'oro-nonexistent-editor-server-qualification' } }
  let inspected = false
  const producers = {
    cpp: async options => {
      assert.equal(options.inspect, true)
      inspected = true
      return modelFor(project, options, 'cpp', { platform: 'linux', arch: 'x86_64' })
    }
  }
  const report = await doctor({ language: 'cpp', sdk: project.root }, project, producers)
  assert.equal(report.ok, false)
  assert.equal(inspected, true)
  assert.equal(existsSync(path.join(project.root, 'out with spaces')), false)
  assert.match(report.checks.find(check => check.name === 'cpp.server').message, /Install it/)
  assert.equal(report.checks.find(check => check.name === 'cpp.model').status, 'warning')
})

test('npm editor startup leaves stdin untouched when setup is unnecessary', () => {
  const project = editorProject()
  const setupModule = new URL('../../npm/src/index.js', import.meta.url).href
  const script = `
    Object.defineProperty(process, 'stdin', { get () { throw new Error('Editor stdin was accessed before launch') } })
    const installation = await import(${JSON.stringify(setupModule)})
    if (!await installation.firstTimeExperienceSetup()) process.exit(1)
  `
  execFileSync(process.execPath, ['--input-type=module', '-e', script, 'language-server'], {
    env: { ...process.env, ORO_ENV_FILE: path.join(project.root, 'absent.env') }, stdio: ['ignore', 'pipe', 'pipe']
  })
})

test('server overrides preserve argument identity and use the Oro project root', () => {
  const root = path.resolve('application with spaces')
  const config = {
    language_server: { cpp: { command: 'clangd-custom', args: '["--background-index"]' } },
    language_server_objc_command: './tools/apple server',
    language_server_objc_args: '["--compile-commands-dir=output with spaces", "", "$(literal)"]'
  }
  const settings = serverSettings(config, 'objc', root)
  assert.equal(settings.command, path.join(root, 'tools/apple server'))
  assert.deepEqual(settings.args, ['--compile-commands-dir=output with spaces', '', '$(literal)'])
  assert.equal(serverSettings(config, 'objcpp', root).command, 'clangd-custom')
  assert.equal(serverSettings(config, 'objc', root, 'override').command, 'override')
  assert.throws(() => serverSettings({}, 'custom', root), /No default server/)
  assert.equal(serverSettings({ language_server_custom_command: 'my-server' }, 'custom', root).adapter, 'stdio')
  assert.throws(() => serverSettings({ language_server_cpp_args: '--shell text' }, 'cpp', root), /array/)
  assert.throws(() => serverSettings({ language_server_cpp_args: '[1]' }, 'cpp', root), /strings/)
  assert.throws(() => serverSettings({ language_server_cpp_unknown: 'value' }, 'cpp', root), /Unknown/)
})

test('LSP framing handles UTF-8 and arbitrary transport fragmentation', async () => {
  const expected = [
    { jsonrpc: '2.0', id: 1, method: 'initialize', params: { rootUri: 'file:///a%20b' } },
    { jsonrpc: '2.0', method: 'textDocument/didOpen', params: { text: 'val emoji = "🌍" // café' } }
  ]
  const received = []
  const stream = new Messages(message => { received.push(message); return [message] })
  const output = []
  stream.on('data', chunk => output.push(chunk))
  const finished = once(stream, 'end')
  const frames = Buffer.concat(expected.map(encode))
  for (const byte of frames) stream.write(Buffer.from([byte]))
  stream.end()
  await finished
  assert.deepEqual(received, expected)
  assert.deepEqual(Buffer.concat(output), frames)
})

test('LSP parser rejects ambiguous, excessive and truncated frames', async () => {
  for (const content of [
    'Content-Length: 2\r\nContent-Length: 3\r\n\r\n{}',
    'Content-Length: -1\r\n\r\n{}',
    'Content-Length: 16777217\r\n\r\n',
    'Content-Length: 20\r\n\r\n{}',
    'Content-Length: 1\r\n\r\n{',
    'Content-Length: 2\r\n\r\n[]',
    'a'.repeat(8193)
  ]) {
    const stream = new Messages(message => [message])
    const failed = once(stream, 'error')
    stream.resume()
    stream.end(Buffer.from(content))
    const [error] = await failed
    assert.ok(error instanceof Error)
  }
})

test('LSP transformations can drop notifications and insert ordered configuration', async () => {
  const stream = new Messages(message => message.method === 'initialized'
    ? [message, { jsonrpc: '2.0', method: 'workspace/didChangeConfiguration', params: {} }]
    : [])
  const output = []
  stream.on('data', chunk => output.push(chunk))
  const finished = once(stream, 'end')
  const initialized = { jsonrpc: '2.0', method: 'initialized', params: {} }
  stream.end(Buffer.concat([encode({ method: 'irrelevant' }), encode(initialized)]))
  await finished
  assert.deepEqual(Buffer.concat(output), Buffer.concat([
    encode(initialized), encode({ jsonrpc: '2.0', method: 'workspace/didChangeConfiguration', params: {} })
  ]))
})
