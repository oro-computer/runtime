'use strict'

const fs = require('node:fs')
const { readAndroidSdk } = require('../android-sdk.cjs')
const path = require('node:path')
const crypto = require('node:crypto')
const { execute } = require('./process.cjs')
const { initializeClient } = require('./lsp.cjs')
const { findConfiguration } = require('./project.cjs')
const { kotlinLaunch, preflightKotlin } = require('./kotlin.cjs')
const { serve } = require('./session.cjs')
const { locked, lease, retain, markGeneration } = require('./cache.cjs')
const { write } = require('./model.cjs')
const { serverSettings, resolveProgram, preflightSourceKit, stdioLaunch } = require('./servers.cjs')
const { nativeModel } = require('./native.cjs')
const { typescriptModel } = require('./typescript.cjs')
const { swiftModel } = require('./swift.cjs')
const { doctor } = require('./doctor.cjs')

const help = `Oro Language Server
  oroc tooling kotlin [--prepare] [--refresh] [--offline] [--classpath] [--variant devDebug] [project-dir]
  oroc tooling <cpp|typescript|swift> [--prepare] [--refresh] [--offline] [--platform name] [--arch name] [--prod] [project-dir]
  oroc tooling doctor [--language name] [--platform name] [--arch name] [--json] [project-dir]
  oroc language-server --language <kotlin|typescript|cpp|c|objc|objcpp|swift|custom> [--server executable] [project-dir]
  --config /path/to/oro.toml selects an explicit project configuration.
  --startup-timeout <ms> bounds preparation and server initialization (default 300000).
Kotlin server launches prepare SDK/resource classes automatically, without compiling application sources.
Otherwise language-server discovers oro.toml from the editor's initialize workspace roots.
Language selection is required. Run one wrapper process per language or language family.
Requires Node.js 22+ and the selected server on PATH. Configure [language_server.<language>] command and args in oro.toml.
C-family language servers are unavailable on Windows in this release; TypeScript editing and tooling cpp model export remain available.
Kotlin Android preparation additionally requires Gradle, Android SDK and a compatible JDK.
The Oro Swift adapter requires SourceKit-LSP from Swift 6.0+ (Xcode 16+ on macOS).
See docs/LANGUAGE_SERVER.md for editor setup and the model format.
`

function parseArguments (argv) {
  const options = { command: argv.shift(), variant: 'devDebug' }
  const flags = new Set(['prepare', 'refresh', 'offline', 'classpath', 'help', 'prod', 'json'])
  const values = new Set(['language', 'variant', 'server', 'config', 'oroc', 'sdk', 'platform', 'arch', 'startup-timeout'])
  if (!['tooling', 'language-server'].includes(options.command)) throw new Error(help)
  for (let index = 0; index < argv.length; index++) {
    const argument = argv[index]
    if (argument === '-h') {
      options.help = true
    } else if (argument.startsWith('--')) {
      const separator = argument.indexOf('=')
      const name = argument.slice(2, separator < 0 ? undefined : separator)
      if (flags.has(name) && separator < 0) options[name] = true
      else if (values.has(name)) {
        const value = separator < 0 ? argv[++index] : argument.slice(separator + 1)
        if (!value || value.startsWith('--')) throw new Error(`--${name} requires a value`)
        options[name] = value
        if (name === 'variant') options.variantExplicit = true
      } else throw new Error(`Unknown option: ${argument}`)
    } else if (options.command === 'tooling' && !options.tool) options.tool = argument
    else if (!options.project) options.project = argument
    else throw new Error(`Unexpected argument: ${argument}`)
  }
  if (options['startup-timeout']) {
    options.startupTimeout = Number(options['startup-timeout'])
    if (!Number.isInteger(options.startupTimeout) || options.startupTimeout < 1000 || options.startupTimeout > 1800000) throw new Error('--startup-timeout must be 1000–1800000 milliseconds')
  }
  if (options.help) return options
  if (options.command === 'language-server' && !options.language) {
    throw new Error('--language is required. Use oroc language-server --language <name>, for example kotlin, typescript, cpp or swift. Run one process per language or language family.')
  }
  if (options.command === 'tooling' && !['kotlin', 'cpp', 'typescript', 'swift', 'doctor'].includes(options.tool)) throw new Error('Available tooling commands: kotlin, cpp, typescript, swift, doctor')
  if (!/^[A-Za-z][A-Za-z0-9]*$/.test(options.variant)) throw new Error('Variant must be an Android variant name, such as devDebug')
  if (options.classpath && options.tool !== 'kotlin') throw new Error('--classpath is only available with tooling kotlin')
  if (options.json && options.tool !== 'doctor') throw new Error('--json is only needed for tooling doctor; model commands already print JSON')
  if (options.tool === 'doctor' && (options.prepare || options.refresh)) throw new Error('Doctor inspects without preparing or refreshing; use the language tooling command to change artifacts')
  if (options.command === 'tooling' && options.language && options.tool !== 'doctor') throw new Error('--language selects a language-server or doctor check; tooling model commands already select their language')
  if (options.command !== 'language-server' && options.server) throw new Error('--server is only available with language-server')
  return options
}

function digest (value) {
  return crypto.createHash('sha256').update(value).digest('hex')
}

function inventory (filename) {
  const stat = fs.lstatSync(filename)
  if (stat.isSymbolicLink()) throw new Error(`Editor build inputs cannot be symbolic links: ${filename}`)
  if (stat.isDirectory()) {
    return [[filename, 'directory'], ...fs.readdirSync(filename).sort().flatMap(name => inventory(path.join(filename, name)))]
  }
  if (!stat.isFile()) throw new Error(`Editor build input is not a regular file: ${filename}`)
  const hash = crypto.createHash('sha256')
  const descriptor = fs.openSync(filename, 'r')
  try {
    const chunk = Buffer.alloc(65536)
    let count
    while ((count = fs.readSync(descriptor, chunk, 0, chunk.length, null))) hash.update(chunk.subarray(0, count))
  } finally {
    fs.closeSync(descriptor)
  }
  return [[filename, hash.digest('hex')]]
}

function classpath (model) {
  return [...new Set([...model.compileClasspath, ...model.runtimeClasses])]
}

function checkArtifacts (model) {
  const missing = classpath(model).filter(filename => !fs.existsSync(filename))
  if (missing.length) {
    throw new Error(`Missing editor artifacts:\n${missing.join('\n')}\nRun oroc tooling kotlin --prepare --variant ${model.variant}. For uncached dependencies, run once without --offline with the required SDK installed.`)
  }
}

async function loadProject (options, initialization) {
  const configuration = findConfiguration(options, initialization)
  const root = path.dirname(configuration)
  const oroc = options.oroc || 'oroc'
  const env = { ...process.env, ORO_LOG_JSON: '', ORO_LOG_NO_COLOR: '1' }
  const resolvedEnvironment = JSON.parse(await execute(oroc, ['env', '--json', configuration], { cwd: root, env }))
  for (const key of ['ANDROID_HOME', 'ANDROID_SDK_ROOT', 'ANDROID_NDK_HOME', 'ANDROID_NDK_ROOT', 'JAVA_HOME', 'GRADLE_HOME', 'GRADLE_USER_HOME', 'ORO_HOME', 'ORO_HOME_API', 'PATH', 'PATHEXT', 'ORO_KOTLIN_LANGUAGE_SERVER', 'CC', 'CXX', 'SDKROOT', 'DEVELOPER_DIR', 'MACOSX_DEPLOYMENT_TARGET', 'IPHONEOS_VERSION_MIN', 'PKG_CONFIG_PATH']) {
    if (resolvedEnvironment[key]) env[key] = resolvedEnvironment[key]
  }
  options.environment = env
  options.sdk ||= env.ORO_HOME
  const configArgs = ['--config', configuration]
  const config = JSON.parse(await execute(oroc, ['config', '--json', ...configArgs], { cwd: root, env }))
  return { configuration, root, oroc, env, configArgs, config }
}

async function kotlinIdentity (options, project) {
  const { configuration, root, oroc, env, config } = project
  if (!options.sdk) throw new Error('Cannot locate the selected Oro SDK; set ORO_HOME or reinstall the CLI')
  const runtime = await readAndroidSdk(oroc, { cwd: root, env })
  // Exporter fixes must also refresh projects created by the same SDK version.
  const inputs = [configuration, path.join(__dirname, 'android-model.gradle')]
  for (const key of ['resources', 'icon']) {
    if (config.android?.[key]) inputs.push(path.resolve(root, config.android[key]))
  }
  for (const name of String(config.android?.manifests || '').split(/[\s,]+/).filter(Boolean)) inputs.push(path.resolve(root, name))
  const environmentInputs = ['.oro.env', '.ororc'].map(name => path.join(root, name))
  if (env.ORO_ENV_FILE) environmentInputs.push(path.resolve(env.ORO_ENV_FILE))
  const inputInventory = inputs.flatMap(inventory).concat(environmentInputs.flatMap(filename => fs.existsSync(filename) ? inventory(filename) : [[filename, null]]))
  const fingerprint = digest(JSON.stringify({
    config,
    runtime,
    root,
    variant: options.variant,
    inputs: inputInventory,
    sdk: options.sdk,
    androidHome: env.ANDROID_HOME,
    androidSdkRoot: env.ANDROID_SDK_ROOT,
    javaHome: env.JAVA_HOME,
    gradleHome: env.GRADLE_HOME
  }))
  const cache = path.resolve(root, config.build?.output || 'build', '.oro-tooling', 'kotlin')
  return { fingerprint, cache, runtime, inputInventory }
}

async function inspectKotlin (options, project) {
  const identity = await kotlinIdentity(options, project)
  const pointer = path.join(identity.cache, `${options.variant}.json`)
  if (!fs.existsSync(pointer)) return { status: 'not-prepared', message: 'Run oroc tooling kotlin --prepare with the selected variant and SDK.' }
  const publication = JSON.parse(fs.readFileSync(pointer, 'utf8'))
  if (publication.fingerprint !== identity.fingerprint) return { status: 'stale' }
  const generation = publication.generation
  const files = fs.readdirSync(generation).filter(file => /^model(?:-.*)?\.json$/.test(file)).map(file => path.join(generation, file))
  files.sort((left, right) => fs.statSync(right).mtimeMs - fs.statSync(left).mtimeMs)
  if (!files.length) return { status: 'not-prepared' }
  const model = JSON.parse(fs.readFileSync(files[0], 'utf8'))
  const missing = classpath(model).filter(file => !fs.existsSync(file))
  return { status: missing.length ? 'missing-artifacts' : 'current', modelFile: files[0], missing, variant: model.variant, compiler: model.compiler, jdk: model.jdk }
}

async function loadModel (options, project) {
  const { root, oroc, env, configArgs } = project
  const { fingerprint, cache, runtime, inputInventory } = await kotlinIdentity(options, project)
  fs.mkdirSync(cache, { recursive: true })
  // A fresh generation is published only after Gradle has successfully exported it.
  // Existing generations can still be used by another editor process.
  const pointer = path.join(cache, `${options.variant}.json`)
  let generation
  if (!options.refresh && fs.existsSync(pointer)) {
    try {
      const previous = JSON.parse(fs.readFileSync(pointer, 'utf8'))
      if (previous.fingerprint === fingerprint) {
        const candidate = path.resolve(previous.generation || '')
        if (path.dirname(candidate) !== cache || (fs.existsSync(candidate) && fs.lstatSync(candidate).isSymbolicLink())) throw new Error('Cached generation must be a directory within this project editor cache')
        generation = candidate
      }
    } catch (error) { throw new Error(`Invalid Kotlin model pointer. Run oroc tooling kotlin --prepare --refresh. ${error.message}`, { cause: error }) }
  }
  if (!generation || !fs.existsSync(path.join(generation, 'oro-editor-inputs.json'))) {
    generation = fs.mkdtempSync(path.join(cache, `${fingerprint.slice(0, 12)}-`))
    try {
      await execute(oroc, ['build', '--platform', 'android', '--editor-project', generation, ...configArgs], {
        cwd: root, env, log: true
      })
    } finally { markGeneration({ generation, projectRoot: root }) }
  }
  const modelFile = path.join(generation, 'model.json')
  const gradle = env.GRADLE_HOME
    ? path.join(env.GRADLE_HOME, 'bin', process.platform === 'win32' ? 'gradle.bat' : 'gradle')
    : 'gradle'
  // Run Gradle every time: its own input tracking and resolver own freshness.
  // Do not infer dependency state from cache filenames or intermediate paths.
  const args = ['--project-dir', generation, '--no-configuration-cache', '--console=plain', '--quiet',
    '--no-daemon', '-Pkotlin.compiler.execution.strategy=in-process',
    `-Poro.editor.variant=${options.variant}`, `-Poro.editor.prepare=${Boolean(options.prepare)}`,
    `-Poro.editor.output=${modelFile}`, ':app:oroEditorModel']
  if (options.offline) args.unshift('--offline')
  try {
    await execute(gradle, args, { cwd: root, env, log: true })
  } catch (error) {
    const recovery = /Querying the mapped value[\s\S]*before task[\s\S]*has completed/.test(error.message)
      ? 'The Oro editor exporter is missing a Gradle producer dependency. Update or relink Oro and retry with --refresh. If this persists, report the full Gradle error; do not edit the generated project.'
      : `Could not export the ${options.variant} editor model. Follow the specific Gradle error above. Prepare required classes with oroc tooling kotlin --prepare --variant ${options.variant}; omit --offline only if the error identifies an uncached dependency. No APK build or installation is required.`
    throw new Error(`${error.message}\n${recovery}`, { cause: error })
  }
  const model = JSON.parse(fs.readFileSync(modelFile, 'utf8'))
  if (model.schemaVersion !== 1 || model.variant !== options.variant) throw new Error('Invalid Gradle editor model; reinstall the Oro SDK')
  model.fingerprint = fingerprint
  model.generation = generation
  model.modelFile = modelFile
  model.pointer = pointer
  model.freshnessInputs = [...inputInventory.map(([filename]) => filename), ...runtime.sources.map(source => path.join(options.sdk, source.path))]
  model.missingArtifacts = classpath(model).filter(filename => !fs.existsSync(filename))
  model.freshnessInputs.push(...model.originalSources.map(file => path.dirname(file)))
  write(modelFile, model)
  const artifacts = digest(JSON.stringify({
    outputs: model.runtimeClasses.flatMap(filename => fs.existsSync(filename) ? inventory(filename) : [[filename, null]]),
    classpath: model.compileClasspath.map(filename => {
      try {
        const stat = fs.statSync(filename)
        return [filename, stat.size, stat.mtimeMs]
      } catch {
        return [filename, null]
      }
    })
  }))
  const publication = JSON.stringify({ generation, artifacts, fingerprint }) + '\n'
  // Equivalent launches must not make concurrent editor sessions restart each other.
  if (!fs.existsSync(pointer) || fs.readFileSync(pointer, 'utf8') !== publication) {
    const temporaryPointer = `${pointer}.${crypto.randomUUID()}`
    fs.writeFileSync(temporaryPointer, publication)
    fs.renameSync(temporaryPointer, pointer)
  }
  return model
}

/**
 * Run the Oro editor model command or a stdio language adapter.
 * @param {string[]} argv
 * @returns {Promise<void>}
 */
async function main (argv) {
  const options = parseArguments([...argv])
  if (options.help) {
    process.stdout.write(help)
    return
  }
  if (Number(process.versions.node.split('.')[0]) < 22) throw new Error('Node.js 22 or later is required; set ORO_NODE_EXECUTABLE to its executable')
  if (options.command === 'language-server') {
    const client = await initializeClient(process.stdin)
    try {
      await serve(client, async () => {
        const launch = await run({ ...options, prepare: options.prepare }, client)
        options.refresh = false
        return launch
      }, options)
    } finally { client.close() }
  } else await run(options)
}

async function run (options, client) {
  const project = await loadProject(options, client?.message.params)
  const producers = { cpp: nativeModel, typescript: typescriptModel, swift: swiftModel }
  if (options.tool === 'doctor') {
    const report = await doctor(options, project, producers, inspectKotlin)
    process.stdout.write(options.json ? JSON.stringify(report, null, 2) + '\n' : report.checks.map(check => `${check.status.toUpperCase()} ${check.name}: ${check.message}`).join('\n') + '\n')
    if (!report.ok) process.exitCode = 1
    return
  }
  if (options.command === 'language-server') {
    const override = options.server || (options.language === 'kotlin' ? project.env.ORO_KOTLIN_LANGUAGE_SERVER : undefined)
    options.backend = serverSettings(project.config, options.language, project.root, override)
    options.backend.command = resolveProgram(options.backend.command, project.env)
    if (options.backend.adapter === 'sourcekit') await preflightSourceKit(options.backend.command, project.env)
    if (options.backend.adapter === 'stdio') {
      if (options.prepare || options.refresh || options.offline) throw new Error('--prepare, --refresh and --offline require an Oro model adapter; stdio delegates discovery to the backend')
      return stdioLaunch(project, options.backend)
    }
  }
  const family = options.backend?.family || options.tool
  if (family === 'kotlin' && (options.platform || options.arch || options.prod)) throw new Error('Kotlin selects its Android target through --variant; --platform, --arch and --prod apply to native models')
  if (family !== 'kotlin' && options.variantExplicit) throw new Error('--variant is only available for the Kotlin Android model')
  if (family === 'typescript' && (options.platform || options.arch || options.prod)) throw new Error('TypeScript compiler settings come from tsconfig/jsconfig; native target options do not apply')
  if (family === 'kotlin') {
    if (options.command === 'language-server') options.prepare = true
    if (options.backend) await preflightKotlin(options.backend.command, project.env, options.backend.adapter)
    const gradle = project.env.GRADLE_HOME ? path.join(project.env.GRADLE_HOME, 'bin', process.platform === 'win32' ? 'gradle.bat' : 'gradle') : 'gradle'
    resolveProgram(gradle, project.env)
  }
  return locked(`${project.root}:${family}`, async () => {
    const cache = path.resolve(project.root, project.config.build?.output || 'build', '.oro-tooling', family)
    retain(cache)
    const producer = family === 'kotlin' ? loadModel : producers[family]
    if (!producer) throw new Error(`No Oro compilation model for ${family}; configure adapter="stdio" for backend-owned discovery`)
    const model = await producer(options, project)
    const release = lease(model)
    try {
      retain(cache)
      if (options.command === 'tooling') {
        if (family === 'kotlin' && (options.prepare || options.classpath)) checkArtifacts(model)
        process.stdout.write(options.classpath ? classpath(model).join(path.delimiter) + '\n' : JSON.stringify(model, null, 2) + '\n')
        release()
        return
      }
      if (family === 'kotlin') checkArtifacts(model)
      const launch = family === 'kotlin' ? await kotlinLaunch(model, options) : stdioLaunch(project, options.backend, model, options)
      const releaseBackend = launch.release
      launch.release = async () => { try { await releaseBackend?.() } finally { release() } }
      return launch
    } catch (error) { release(); throw error }
  })
}

module.exports = { main }
