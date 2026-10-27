'use strict'

const { markGeneration } = require('./cache.cjs')

const fs = require('node:fs')
const path = require('node:path')
const { createRequire } = require('node:module')
const { setting, words, modelFor, write, publish } = require('./model.cjs')
const { resolveProgram } = require('./servers.cjs')

function typescriptPath (project) {
  const configured = setting(project.config, 'tooling.typescript.typescript')
  if (configured) return fs.realpathSync(path.resolve(project.root, configured, 'lib', 'typescript.js'))
  const roots = [project.root]
  try { roots.push(path.dirname(resolveProgram('tsserver', project.env))) } catch {}
  for (const root of roots) {
    try { return createRequire(path.join(root, 'oro-tooling.cjs')).resolve('typescript/lib/typescript.js') } catch {}
  }
  throw new Error('TypeScript is unavailable. Install typescript in the project or put tsserver on PATH; tooling.typescript.typescript can select an installed package directory.')
}

function sdkTypings (options, project) {
  const candidates = [
    options.sdk && path.join(options.sdk, 'api', 'index.d.ts'),
    project.env.ORO_HOME_API && path.join(project.env.ORO_HOME_API, 'index.d.ts')
  ].filter(Boolean)
  for (const candidate of candidates) if (fs.existsSync(candidate)) return fs.realpathSync(candidate)
  throw new Error('Selected Oro SDK typings are missing. Reinstall the SDK with its generated API declarations; no application declaration files need to be edited.')
}

/**
 * Export an isolated TypeScript project while preserving application compiler settings.
 * @param {object} options
 * @param {object} project
 * @returns {Promise<object>}
 */
async function typescriptModel (options, project) {
  const compilerFile = typescriptPath(project)
  const ts = require(compilerFile)
  if (Number(ts.versionMajorMinor.split('.')[0]) < 5) throw new Error(`TypeScript ${ts.version} is unsupported; install TypeScript 5 or later`)
  const typings = sdkTypings(options, project)
  const explicit = setting(project.config, 'tooling.typescript.tsconfig')
  const configFile = explicit ? path.resolve(project.root, explicit) : ['tsconfig.json', 'jsconfig.json'].map(name => path.join(project.root, name)).find(filename => fs.existsSync(filename))
  const inputs = [__filename, path.join(__dirname, 'typescript-plugin.cjs'), compilerFile, typings,
    ...['tsconfig.json', 'jsconfig.json', 'package.json'].map(filename => path.join(project.root, filename))]
  const configurations = new Map()
  const format = diagnostics => ts.formatDiagnostics(diagnostics, { getCanonicalFileName: file => file, getCurrentDirectory: () => project.root, getNewLine: () => '\n' })
  const readConfig = filename => {
    filename = fs.realpathSync(filename)
    if (configurations.has(filename)) return configurations.get(filename)
    const errors = []
    const parsed = ts.getParsedCommandLineOfConfigFile(filename, {}, {
      ...ts.sys,
      readFile: file => { if (file.endsWith('.json')) inputs.push(file); return ts.sys.readFile(file) },
      onUnRecoverableConfigFileDiagnostic: diagnostic => errors.push(diagnostic)
    })
    if (!parsed || errors.length || parsed.errors.length) throw new Error(format([...errors, ...(parsed?.errors || [])]))
    configurations.set(filename, parsed)
    for (const reference of parsed.projectReferences || []) readConfig(ts.resolveProjectReferencePath(reference))
    return parsed
  }
  let parsed
  let sourceMode = configFile ? 'configured' : 'declared'
  if (configFile) parsed = readConfig(configFile)
  else {
    const sources = words(setting(project.config, 'tooling.typescript.sources'))
    if (!sources.length) {
      const copy = String(setting(project.config, 'build.copy', ''))
      for (const item of copy.split(';').map(item => item.trim()).filter(Boolean)) {
        const source = item.split('=')[0].trim()
        if (source) sources.push(source)
      }
    }
    if (!sources.length) {
      // A build script does not expose a static compiler model. Let tsserver
      // infer projects from opened documents and their imports, without a root scan.
      sourceMode = 'inferred'
      parsed = { fileNames: [] }
    } else {
      const include = sources.map(source => {
        const absolute = path.resolve(project.root, source)
        if (!fs.existsSync(absolute)) throw new Error(`Declared web source is missing: ${absolute}`)
        return fs.statSync(absolute).isDirectory() ? path.join(absolute, '**', '*') : absolute
      })
      parsed = ts.parseJsonConfigFileContent({ compilerOptions: { allowJs: true, checkJs: false, noEmit: true, target: 'ES2022', module: 'ESNext', moduleResolution: 'Bundler', skipLibCheck: true }, include, exclude: [path.resolve(project.root, setting(project.config, 'build.output', 'build')), '**/node_modules/**'] }, ts.sys, project.root)
      if (parsed.errors.length) throw new Error(format(parsed.errors))
    }
    configurations.set('', parsed)
  }
  const sourceFiles = [...new Set([...configurations.values()].flatMap(config => config.fileNames).map(file => fs.realpathSync(file)))]
  if (!sourceFiles.length && sourceMode !== 'inferred') throw new Error('The TypeScript project does not contain any declared source files')
  inputs.push(...sourceFiles.map(file => path.dirname(file)))
  const model = modelFor(project, options, 'typescript', { platform: 'web', sourceMode, typescriptVersion: ts.version, compiler: compilerFile, typings, tsconfig: configFile || null, originalSources: sourceFiles }, inputs)
  const generatedConfig = path.join(model.generation, 'tsconfig.json')
  const pluginDirectory = path.join(model.generation, 'node_modules', 'oro-typescript-plugin')
  model.editorConfig = generatedConfig
  model.plugin = { name: 'oro-typescript-plugin', location: model.generation }
  model.artifacts = [generatedConfig, path.join(pluginDirectory, 'package.json'), path.join(pluginDirectory, 'index.cjs'), typings]
  model.preparation = sourceMode === 'inferred'
    ? 'Adds selected SDK declarations to tsserver projects inferred from opened documents and their imports. No repository source scan or build-script execution.'
    : 'Uses the selected SDK declarations and installed TypeScript; it does not install packages or emit application JavaScript.'
  if (options.inspect) return model
  markGeneration(model)
  const configs = [...configurations.keys()]
  const destination = filename => filename === (configFile ? fs.realpathSync(configFile) : '') ? generatedConfig : path.join(model.generation, `reference-${configs.indexOf(filename)}`, 'tsconfig.json')
  for (const [filename, config] of configurations) {
    const generated = {
      ...(filename ? { extends: filename } : { compilerOptions: { allowJs: true, checkJs: false, noEmit: true, target: 'ES2022', module: 'ESNext', moduleResolution: 'Bundler', skipLibCheck: true } }),
      files: [...config.fileNames, typings],
      include: [],
      exclude: [],
      references: (config.projectReferences || []).map(reference => ({ path: destination(fs.realpathSync(ts.resolveProjectReferencePath(reference))) }))
    }
    write(destination(filename), generated)
    model.artifacts.push(destination(filename))
  }
  write(path.join(pluginDirectory, 'package.json'), { name: 'oro-typescript-plugin', version: '1.0.0', main: 'index.cjs' })
  write(path.join(pluginDirectory, 'index.cjs'), fs.readFileSync(path.join(__dirname, 'typescript-plugin.cjs'), 'utf8'))
  return publish(model)
}

module.exports = { typescriptModel }
