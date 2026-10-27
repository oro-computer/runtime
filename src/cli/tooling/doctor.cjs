'use strict'

const fs = require('node:fs')
const path = require('node:path')
const { serverSettings, resolveProgram } = require('./servers.cjs')
const { setting, freshness } = require('./model.cjs')
const { execute } = require('./process.cjs')

/**
 * Inspect configured editors and model freshness without preparing artifacts.
 * @param {object} options
 * @param {object} project
 * @param {object} producers
 * @param {Function} inspectKotlin
 * @returns {Promise<object>}
 */
async function doctor (options, project, producers, inspectKotlin) {
  const checks = []
  const record = (name, status, message, details) => checks.push({ name, status, message, ...(details ? { details } : {}) })
  record('project', 'ok', project.configuration)
  record('node', 'ok', process.version)
  record('sdk', options.sdk && fs.existsSync(options.sdk) ? 'ok' : 'error', options.sdk || 'Cannot locate selected SDK; reinstall Oro or set ORO_HOME.')
  const languages = new Set(options.language ? [options.language] : [])
  if (!options.language) {
    if (setting(project.config, 'android.sources')) languages.add('kotlin')
    if (['tsconfig.json', 'jsconfig.json', 'package.json'].some(file => fs.existsSync(path.join(project.root, file))) || setting(project.config, 'build.copy') || setting(project.config, 'tooling.typescript.sources')) languages.add('typescript')
    const flattened = JSON.stringify(project.config)
    if (/build_extensions|"extensions"|tooling_cpp/.test(flattened) || project.config.tooling?.cpp) languages.add('cpp')
    if (/tooling_swift/.test(flattened) || project.config.tooling?.swift) languages.add('swift')
    for (const language of Object.keys(project.config.language_server || {})) languages.add(language)
    for (const key of Object.keys(project.config)) {
      const match = /^language_server_(.+)_(command|adapter|args)$/.exec(key)
      if (match) languages.add(match[1])
    }
  }
  if (!languages.size) record('languages', 'warning', 'No declared language scope found. Select --language kotlin, typescript, cpp or swift to inspect a specific toolchain.')
  const inspected = new Set()
  for (const language of languages) {
    let settings
    try { settings = serverSettings(project.config, language, project.root) } catch (error) { record(language, 'error', error.message); continue }
    if (inspected.has(settings.family)) continue
    inspected.add(settings.family)
    try { record(`${language}.server`, 'ok', resolveProgram(settings.command, project.env)) } catch (error) { record(`${language}.server`, 'error', error.message) }
    if (settings.adapter === 'stdio') {
      record(`${language}.model`, 'ok', 'Backend owns project discovery (adapter=stdio).')
      continue
    }
    if (settings.family === 'kotlin') {
      const androidSdk = project.env.ANDROID_HOME || project.env.ANDROID_SDK_ROOT
      record('kotlin.android-sdk', androidSdk && fs.existsSync(androidSdk) ? 'ok' : 'error', androidSdk && fs.existsSync(androidSdk) ? androidSdk : 'Install the selected Android SDK and set ANDROID_HOME in the editor launch environment.')
      for (const [name, home] of [['java', project.env.JAVA_HOME], ['gradle', project.env.GRADLE_HOME]]) {
        try {
          const executable = resolveProgram(home ? path.join(home, 'bin', name + (process.platform === 'win32' ? name === 'java' ? '.exe' : '.bat' : '')) : name, project.env)
          record(`kotlin.${name}`, 'ok', executable)
          if (name === 'java') {
            const version = await execute(executable, ['--version'], { env: project.env, timeout: 10000 })
            const major = Number(/(?:openjdk|java) (\d+)/.exec(version)?.[1])
            record('kotlin.java-version', major >= 21 ? 'ok' : 'error', major >= 21 ? version.trim().split('\n')[0] : `Kotlin editor preparation requires Java 21 or later. Set JAVA_HOME; detected ${version.trim()}`)
          }
        } catch (error) { record(`kotlin.${name}`, 'error', error.message) }
      }
    }
    try {
      const model = settings.family === 'kotlin' ? null : await producers[settings.family]({ ...options, inspect: true }, project)
      const state = model ? freshness(model) : await inspectKotlin(options, project)
      const status = state.status === 'current' ? 'ok' : state.status === 'not-prepared' || state.status === 'stale' ? 'warning' : 'error'
      record(`${language}.model`, status, `${state.status}${state.status === 'current' ? '' : `; run oroc tooling ${settings.family} --prepare with the selected target`}`, { ...state, ...(model ? { platform: model.platform, arch: model.arch, sourceMode: model.sourceMode, toolchain: model.toolchain, missingArtifacts: model.missingArtifacts } : {}) })
    } catch (error) { record(`${language}.model`, 'error', error.message) }
  }
  return { schemaVersion: 1, projectRoot: project.root, ok: !checks.some(check => check.status === 'error'), checks }
}

module.exports = { doctor }
