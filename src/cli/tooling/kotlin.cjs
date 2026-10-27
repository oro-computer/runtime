'use strict'

const fs = require('node:fs')
const path = require('node:path')
const { execute } = require('./process.cjs')
const { watchModel } = require('./watch.cjs')
const { scopeGuard } = require('./scope.cjs')
const { pathToFileURL } = require('node:url')

function quoteShell (value) {
  return `'${value.replaceAll("'", "'\\''")}'`
}

/**
 * Validate the server and Java before running Gradle.
 * @param {string} server Resolved backend launcher.
 * @param {object} environment Launch environment.
 * @param {string} [adapter]
 * @returns {Promise<string>} Selected Java executable.
 */
async function preflightKotlin (server, environment, adapter = 'fwcd') {
  if (adapter === 'fwcd') {
    const lib = path.resolve(path.dirname(server), '..', 'lib')
    if (!fs.existsSync(lib)) throw new Error('Use the bin/kotlin-language-server launcher from the fwcd 1.3.13 distribution')
    const libraries = fs.readdirSync(lib)
    if (!libraries.some(name => /^(?:server|kotlin-language-server)-1\.3\.13\.jar$/.test(name)) || !libraries.includes('kotlin-compiler-2.1.0.jar')) {
      throw new Error('This adapter supports kotlin-language-server 1.3.13 with Kotlin compiler 2.1.0. Install that distribution or select the kotlin-lsp adapter.')
    }
  }
  const java = environment.JAVA_HOME
    ? path.join(environment.JAVA_HOME, 'bin', process.platform === 'win32' ? 'java.exe' : 'java')
    : 'java'
  const version = await execute(java, ['--version'], { timeout: 10000, env: environment })
  const major = Number(/(?:openjdk|java) (\d+)/.exec(version)?.[1])
  if (!(major >= 21)) throw new Error(`Kotlin editor preparation requires JDK 21 or later; detected ${version.trim().split('\n')[0]}. Set JAVA_HOME in the editor launch environment.`)
  const sdk = environment.ANDROID_HOME || environment.ANDROID_SDK_ROOT
  if (!sdk || !fs.existsSync(sdk)) throw new Error('Android SDK is unavailable. Install the SDK selected by Oro and set ANDROID_HOME in the editor launch environment.')
  return java
}

async function checkArtifacts (server, model, environment) {
  const java = environment.JAVA_HOME ? path.join(environment.JAVA_HOME, 'bin', process.platform === 'win32' ? 'java.exe' : 'java') : 'java'
  if (Number(model.compiler.jvmTarget) > 23) throw new Error(`JVM target ${model.compiler.jvmTarget} is unsupported by fwcd 1.3.13; use a newer Oro adapter`)
  // Inspect only artifacts returned by Gradle, including transitive JAR metadata.
  // The Java source launcher avoids requiring another installed scripting runtime.
  try {
    await execute(java, [path.join(__dirname, 'KotlinCompatibility.java'), model.modelFile + '.classpath'], { timeout: 120000, env: environment })
  } catch (error) {
    throw new Error(`${error.message}\nThe Kotlin adapter requires JDK 21+. Set JAVA_HOME accordingly. For unsupported Kotlin metadata, use a compatible Oro adapter; do not disable metadata checks.`)
  }
}

/**
 * Prepare a Kotlin backend using the selected Gradle model.
 * @param {object} model Exported Android compilation model.
 * @param {object} options Selected backend and launch environment.
 * @returns {Promise<object>} Supervisor launch descriptor.
 */
async function kotlinLaunch (model, options) {
  const server = options.backend.command
  const classpath = [...new Set([...model.compileClasspath, ...model.runtimeClasses])]
  if (classpath.some(filename => filename.includes(path.delimiter) || /[\r\n]/.test(filename))) {
    throw new Error('The Kotlin classpath cannot represent paths containing line breaks or the platform classpath separator')
  }
  const environment = options.environment || process.env
  const session = fs.mkdtempSync(path.join(model.generation, 'lsp-'))
  fs.writeFileSync(path.join(session, '.oro-session.json'), JSON.stringify({ pid: process.pid }))
  try {
    let folders
    let env = environment
    const workspace = path.join(session, 'workspace')
    fs.mkdirSync(workspace)
    if (options.backend.adapter === 'kotlin-lsp') {
      const { jetbrainsWorkspace } = require('./kotlin-workspace.cjs')
      fs.writeFileSync(path.join(workspace, 'workspace.json'), JSON.stringify(jetbrainsWorkspace(model), null, 2))
      folders = [{ uri: pathToFileURL(workspace).href, name: 'Oro editor model' }]
    } else {
      fs.writeFileSync(model.modelFile + '.classpath', classpath.join('\n') + '\n')
      await checkArtifacts(server, model, environment)
      const configHome = path.join(session, 'config')
      const hookDirectory = path.join(configHome, 'kotlin-language-server')
      fs.mkdirSync(hookDirectory, { recursive: true })
      const hook = process.platform === 'win32' ? 'classpath.cmd' : 'classpath'
      const classpathFile = path.join(hookDirectory, 'classpath.txt')
      fs.writeFileSync(classpathFile, classpath.join(path.delimiter) + '\n')
      fs.writeFileSync(path.join(hookDirectory, hook), process.platform === 'win32'
        ? '@echo off\r\ntype "%~dp0classpath.txt"\r\n'
        : `#!/bin/sh\nexec cat ${quoteShell(classpathFile)}\n`, { mode: 0o700 })
      env = { ...environment, XDG_CONFIG_HOME: configHome }
      folders = [{ uri: pathToFileURL(workspace).href, name: 'Oro editor model' }, ...model.originalSources.map(file => ({ uri: pathToFileURL(file).href, name: path.basename(file) }))]
    }
    const scope = scopeGuard(model)
    return {
      command: server,
      args: options.backend.args,
      cwd: model.projectRoot,
      env,
      language: 'kotlin',
      model,
      initializationProgressTitle: options.backend.adapter === 'fwcd' && model.originalSources.some(file => /\.kts?$/.test(file)) ? 'Kotlin: Indexing' : undefined,
      ...scope,
      transform (message) {
        if (options.backend.adapter === 'fwcd' && message.method === 'workspace/didChangeConfiguration') {
          const settings = message.params.settings ||= {}
          const kotlin = settings.kotlin ||= {}
          const compiler = kotlin.compiler ||= {}
          const jvm = compiler.jvm ||= {}
          jvm.target = model.compiler.jvmTarget
        }
        return scope.transform(message)
      },
      watch: watchModel(model),
      initialize (message) {
        message.params = {
          ...message.params,
          rootUri: folders[0].uri,
          rootPath: workspace,
          workspaceFolders: folders,
          initializationOptions: { ...message.params.initializationOptions, storagePath: path.join(session, 'storage') }
        }
        return message
      },
      // This pinned backend accepts compiler configuration before initialize,
      // so its initial source analysis uses the Gradle JVM target too.
      beforeInitialize: options.backend.adapter === 'fwcd'
        ? [{
            method: 'workspace/didChangeConfiguration',
            params: { settings: { kotlin: { compiler: { jvm: { target: model.compiler.jvmTarget } } } } }
          }]
        : [],
      release: () => fs.rmSync(session, { recursive: true, force: true })
    }
  } catch (error) {
    fs.rmSync(session, { recursive: true, force: true })
    throw error
  }
}

module.exports = { kotlinLaunch, preflightKotlin }
