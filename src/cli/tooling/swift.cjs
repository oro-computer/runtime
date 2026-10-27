'use strict'

const { markGeneration } = require('./cache.cjs')

const fs = require('node:fs')
const path = require('node:path')
const { execute } = require('./process.cjs')
const { resolveProgram } = require('./servers.cjs')
const { appleToolchain } = require('./native.cjs')
const { setting, words, target, modelFor, write, publish } = require('./model.cjs')

/**
 * Export Swift compiler commands and optionally emit an analysis module.
 * @param {object} options
 * @param {object} project
 * @returns {Promise<object>}
 */
async function swiftModel (options, project) {
  const identity = target(options, project.config, 'swift')
  const database = setting(project.config, 'tooling.swift.compilation_database')
  if (identity.platform === 'android' && !database) throw new Error('Swift Android tooling requires an exported compilation database and a supported Swift Android toolchain; automatic Android Swift preparation is not supported')
  const inputs = [__filename]
  let entries
  let toolchain
  let sources
  let flags
  const moduleName = setting(project.config, 'tooling.swift.module', 'OroApplication')
  if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(moduleName)) throw new Error('tooling.swift.module must be a Swift module identifier')
  if (database) {
    const filename = path.resolve(project.root, database)
    inputs.push(filename)
    const commands = JSON.parse(fs.readFileSync(filename, 'utf8'))
    if (!Array.isArray(commands) || commands.length > 100000) throw new Error('Swift compilation database must contain at most 100000 commands')
    entries = commands.filter(entry => typeof entry.file === 'string' && entry.file.endsWith('.swift')).map(entry => {
      if (typeof entry.directory !== 'string') throw new Error('Swift compilation database entries require directory and file')
      const directory = path.resolve(path.dirname(filename), entry.directory)
      const args = words(entry.arguments || entry.command)
      if (!args.length) throw new Error(`Missing Swift compiler command for ${entry.file}`)
      return { directory, file: fs.realpathSync(path.resolve(directory, entry.file)), arguments: args }
    })
    sources = [...new Set(entries.map(entry => entry.file))]
    if (options.prepare) throw new Error('Preparation of an external Swift compilation database belongs to its build system. Prepare its modules there, then run oroc tooling swift without --prepare; Oro will not replay commands that could link or sign an app.')
  } else {
    sources = words(setting(project.config, 'tooling.swift.sources')).map(file => fs.realpathSync(path.resolve(project.root, file)))
    if (!sources.length) throw new Error('Declare original Swift files in tooling.swift.sources, or select tooling.swift.compilation_database. SwiftPM projects can use language_server.swift.adapter="stdio" for SourceKit-LSP native package discovery.')
    for (const source of sources) if (!source.endsWith('.swift') || !fs.statSync(source).isFile()) throw new Error(`Expected an original Swift source file: ${source}`)
    if (['mac', 'ios', 'ios-simulator'].includes(identity.platform)) toolchain = await appleToolchain(identity, project, 'swiftc')
    else {
      const host = process.platform === 'linux' ? 'linux' : 'win'
      if (identity.platform !== host) throw new Error('Cross-platform Swift modules require an exported compilation database')
      const executable = resolveProgram('swiftc', project.env)
      const info = JSON.parse(await execute(executable, ['-print-target-info'], { env: project.env, timeout: 30000 }))
      const expectedArch = { x64: 'x86_64', ia32: 'x86' }[process.arch] || process.arch
      if (identity.arch !== expectedArch) throw new Error('Cross-architecture Swift modules require an exported compilation database')
      toolchain = { executable, triple: info.target.triple, compilerVersion: info.compilerVersion }
    }
    toolchain.compilerVersion ||= (await execute(toolchain.executable, ['--version'], { env: project.env, timeout: 30000 })).trim()
    flags = ['-module-name', moduleName, '-target', toolchain.triple, ...(identity.buildType === 'debug' ? ['-D', 'DEBUG', '-Onone'] : ['-O'])]
    if (toolchain.sysroot) flags.push('-sdk', toolchain.sysroot)
    for (const [key, flag] of [['module_paths', '-I'], ['framework_paths', '-F']]) {
      for (const value of words(setting(project.config, `tooling.swift.${key}`))) {
        const directory = path.resolve(project.root, value)
        if (!fs.existsSync(directory)) throw new Error(`Missing Swift ${key}: ${directory}. Prepare the dependency with its build system first.`)
        inputs.push(directory)
        flags.push(flag, directory)
      }
    }
    const bridge = setting(project.config, 'tooling.swift.bridging_header')
    if (bridge) {
      const filename = fs.realpathSync(path.resolve(project.root, bridge))
      inputs.push(filename)
      flags.push('-import-objc-header', filename)
    }
    const extra = words(setting(project.config, 'tooling.swift.compiler_flags'))
    if (extra.some(flag => /^(?:-o|-emit-(?:executable|library|object)|-c|-sdk|-target|-module-name|-emit-module-path)(?:=|$)/.test(flag) || flag.startsWith('@'))) {
      throw new Error('Swift editor compiler_flags cannot override target, SDK, module identity, output or compilation mode; use the dedicated tooling.swift settings')
    }
    flags.push(...extra)
    inputs.push(...sources)
  }
  if (!sources.length) throw new Error('No original Swift sources found in the selected compilation database')
  const model = modelFor(project, options, 'swift', { ...identity, provider: database ? 'compilation-database' : 'module', toolchain, moduleName, originalSources: sources }, inputs)
  model.compileCommands = path.join(model.generation, 'compile_commands.json')
  model.artifacts = [model.compileCommands, ...sources, ...(toolchain ? [toolchain.executable] : [])]
  if (!database) {
    model.moduleFile = path.join(model.generation, 'Modules', `${moduleName}.swiftmodule`)
    model.artifacts.push(model.moduleFile)
    entries = sources.map(file => ({ file, directory: project.root, arguments: [toolchain.executable, ...flags, ...sources] }))
    if (options.prepare && !options.inspect) {
      markGeneration(model)
      fs.mkdirSync(path.dirname(model.moduleFile), { recursive: true })
      await execute(toolchain.executable, [...flags, '-emit-module', '-emit-module-path', model.moduleFile, '-module-cache-path', path.join(model.generation, 'ModuleCache'), ...sources], { cwd: project.root, env: project.env, log: true })
    }
    if (!fs.existsSync(model.moduleFile)) model.missingArtifacts.push(model.moduleFile)
  }
  model.commands = entries
  model.preparation = database ? 'External build system owns dependency and module preparation.' : 'Emits a Swift analysis module only; no executable linking, signing, installation or package downloads.'
  if (options.inspect) return model
  markGeneration(model)
  write(model.compileCommands, entries)
  return publish(model)
}

module.exports = { swiftModel }
