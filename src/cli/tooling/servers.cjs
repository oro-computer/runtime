'use strict'

const fs = require('node:fs')
const path = require('node:path')
const { pathToFileURL } = require('node:url')
const { watchModel } = require('./watch.cjs')
const { scopeGuard } = require('./scope.cjs')
const { execute } = require('./process.cjs')

const defaults = {
  kotlin: { command: 'kotlin-language-server', args: [], adapter: 'fwcd' },
  typescript: { command: 'typescript-language-server', args: ['--stdio'], adapter: 'typescript' },
  cpp: { command: 'clangd', args: [], adapter: 'clangd' },
  swift: { command: 'sourcekit-lsp', args: [], adapter: 'sourcekit' }
}
const aliases = {
  kt: 'kotlin',
  web: 'typescript',
  ts: 'typescript',
  js: 'javascript',
  jsx: 'javascriptreact',
  tsx: 'typescriptreact',
  'c++': 'cpp',
  'objective-c': 'objc',
  'objective-c++': 'objcpp'
}
const families = { javascript: 'typescript', javascriptreact: 'typescript', typescriptreact: 'typescript', c: 'cpp', objc: 'cpp', objcpp: 'cpp' }

function settingsFor (config, language) {
  const result = { ...config.language_server?.[language] }
  const prefix = `language_server_${language}_`
  for (const [key, value] of Object.entries(config)) {
    if (key.startsWith(prefix)) result[key.slice(prefix.length)] = value
  }
  for (const key of Object.keys(result)) {
    if (!['command', 'args', 'adapter'].includes(key)) throw new Error(`Unknown language_server.${language}.${key}; expected command, args or adapter`)
  }
  return result
}

/**
 * Resolve project server settings over a language family's defaults.
 * @param {object} config Configuration returned by oroc config --json.
 * @param {string} language Requested LSP language or alias.
 * @param {string} root Directory containing oro.toml (or legacy oro.ini).
 * @param {string} [override] Explicit --server executable.
 * @returns {object} Executable, arguments, adapter and normalized language.
 */
function serverSettings (config, language, root, override) {
  language = aliases[language] || language
  if (!/^[a-z][a-z0-9_]*$/.test(language)) throw new Error(`Invalid language name: ${language}`)
  const family = families[language] || language
  if (process.platform === 'win32' && family === 'cpp') {
    throw new Error('C/C++ language-server support on Windows is deferred to a future release. Use a Linux or macOS host for C/C++ editor integration.')
  }
  const configured = { ...settingsFor(config, family), ...settingsFor(config, language) }
  const base = family === 'kotlin' && configured.adapter === 'kotlin-lsp' ? { command: 'kotlin-lsp', args: ['--stdio'], adapter: 'kotlin-lsp' } : defaults[family]
  const settings = { ...base, ...configured }
  settings.command = override || settings.command
  if (typeof settings.command !== 'string' || !settings.command.trim()) {
    throw new Error(`No default server for ${language}. Set [language_server.${language}] command and args in oro.toml.`)
  }
  if (/[\r\n]/.test(settings.command) || settings.command.includes('\0')) throw new Error('Language server command must be a single executable path or PATH name')
  if (settings.command.includes('/') || settings.command.includes('\\')) settings.command = path.resolve(root, settings.command)
  settings.args ??= []
  if (typeof settings.args === 'string') {
    try { settings.args = JSON.parse(settings.args) } catch { throw new Error(`language_server.${language}.args must be a TOML string array (or a JSON string array in oro.ini)`) }
  }
  if (!Array.isArray(settings.args) || settings.args.some(argument => typeof argument !== 'string' || argument.includes('\0'))) {
    throw new Error(`language_server.${language}.args must contain only strings without NUL characters`)
  }
  settings.adapter ||= 'stdio'
  if (settings.adapter !== 'stdio' && settings.adapter !== defaults[family]?.adapter && !(family === 'kotlin' && settings.adapter === 'kotlin-lsp')) {
    throw new Error(`Unsupported adapter for ${language}: ${settings.adapter}. Use ${family === 'kotlin' ? 'fwcd or kotlin-lsp' : defaults[family]?.adapter || 'stdio'} for the Oro model, or stdio for backend-owned project discovery.`)
  }
  return { ...settings, language, family }
}

/**
 * @typedef {object} ProgramResolutionOptions
 * @property {boolean} [preserveSymlinks] Preserve the executable name for multicall drivers.
 */

/**
 * Find the configured program without invoking a shell or installing packages.
 * @param {string} command
 * @param {object} environment
 * @param {ProgramResolutionOptions} [options]
 * @returns {string}
 */
function resolveProgram (command, environment, options = {}) {
  const directories = path.isAbsolute(command) ? [''] : (environment.PATH || '').split(path.delimiter)
  const extensions = process.platform === 'win32' && !path.extname(command)
    ? (environment.PATHEXT || '.COM;.EXE;.BAT;.CMD').split(';').map(extension => extension.toLowerCase())
    : ['']
  for (const directory of directories) {
    for (const extension of extensions) {
      const candidate = path.resolve(directory, command + extension)
      try {
        if (!fs.statSync(candidate).isFile()) continue
        fs.accessSync(candidate, process.platform === 'win32' ? fs.constants.F_OK : fs.constants.X_OK)
        return options.preserveSymlinks ? candidate : fs.realpathSync(candidate)
      } catch {}
    }
  }
  throw new Error(`Program '${command}' is unavailable. Install it on the editor's PATH. For a language server, set language_server.<language>.command in oro.toml; for a build tool, select its documented toolchain path. Oro does not download servers automatically.`)
}

/**
 * Check the selected SourceKit-LSP supports Oro's compilation database setup.
 * @param {string} command Resolved SourceKit-LSP executable.
 * @param {object} environment Selected editor environment.
 * @returns {Promise<void>}
 */
async function preflightSourceKit (command, environment) {
  const help = await execute(command, ['--help'], { env: environment, timeout: 10000 })
  const required = ['--default-workspace-type', '--compilation-db-search-path']
  const missing = required.filter(option => !new RegExp(`(?:^|\\s)${option}(?:\\s|=|$)`).test(help))
  if (missing.length) {
    throw new Error(`Oro Swift editor integration requires SourceKit-LSP from Swift 6.0 or later (Xcode 16 or later on macOS). Selected backend: ${command}. Missing options: ${missing.join(', ')}. Select a supported toolchain with DEVELOPER_DIR or language_server.swift.command.`)
  }
}

/** Build an editor-independent launch descriptor for the shared supervisor. */
function stdioLaunch (project, settings, model, options = {}) {
  const args = [...settings.args]
  if (settings.adapter === 'clangd' && model) {
    if (args.some(arg => arg.startsWith('--compile-commands-dir'))) throw new Error('Oro owns --compile-commands-dir. Select tooling.cpp.compilation_database or use adapter="stdio" to manage it yourself.')
    args.push(`--compile-commands-dir=${model.generation}`)
  }
  if (settings.adapter === 'sourcekit' && model) {
    if (args.some(arg => /^--(?:compilation-db-search-path|default-workspace-type)(?:=|$)/.test(arg))) throw new Error('Oro owns the SourceKit-LSP workspace and compilation database options. Use adapter="stdio" to manage them yourself.')
    args.push('--default-workspace-type', 'compilationDatabase', '--compilation-db-search-path', path.relative(project.root, model.generation))
  }
  const scope = model ? scopeGuard(model) : {}
  return {
    command: settings.command,
    args,
    cwd: project.root,
    language: settings.language,
    env: { ...project.env, ...(settings.adapter === 'typescript' && model ? { ORO_TYPESCRIPT_MODEL: model.modelFile } : {}) },
    model,
    ...scope,
    watch: watchModel(model || { configuration: project.configuration }),
    initialize (message) {
      message.params = {
        ...message.params,
        rootUri: pathToFileURL(project.root).href,
        rootPath: project.root,
        workspaceFolders: [{ uri: pathToFileURL(project.root).href, name: path.basename(project.root) }]
      }
      const initial = message.params.initializationOptions || {}
      if (settings.adapter === 'typescript' && model) {
        message.params.initializationOptions = {
          ...initial,
          plugins: [...(initial.plugins || []).filter(plugin => plugin.name !== model.plugin.name), model.plugin],
          tsserver: { ...initial.tsserver, path: path.join(path.dirname(model.compiler), 'tsserver.js') },
          ...(options.offline ? { disableAutomaticTypingAcquisition: true } : {})
        }
      }
      if (settings.adapter === 'sourcekit' && model) {
        message.params.initializationOptions = {
          ...initial,
          defaultWorkspaceType: 'compilationDatabase',
          compilationDatabase: { ...initial.compilationDatabase, searchPaths: [path.relative(project.root, model.generation)] },
          ...(model.indexStorePath ? { index: { ...initial.index, indexStorePath: model.indexStorePath, indexDatabasePath: model.indexDatabasePath } } : {})
        }
      }
      return message
    }
  }
}

module.exports = { serverSettings, resolveProgram, preflightSourceKit, stdioLaunch }
