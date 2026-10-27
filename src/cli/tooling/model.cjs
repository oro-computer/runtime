'use strict'

const fs = require('node:fs')
const path = require('node:path')
const crypto = require('node:crypto')

/**
 * Read a configuration value from either CLI JSON or a nested object.
 * @param {object} config
 * @param {string} name
 * @param {*} [fallback]
 * @returns {*}
 */
function setting (config, name, fallback) {
  if (Object.hasOwn(config, name.replaceAll('.', '_'))) return config[name.replaceAll('.', '_')]
  let value = config
  for (const part of name.split('.')) value = value?.[part]
  return value ?? fallback
}

/**
 * Parse literal argv/source lists. No shell substitutions are executed.
 * @param {string|string[]|undefined} value
 * @returns {string[]}
 */
function words (value) {
  if (value == null || value === '') return []
  if (Array.isArray(value)) {
    if (value.some(item => typeof item !== 'string' || item.includes('\0'))) throw new Error('Expected an array of strings without NUL characters')
    return [...value]
  }
  if (typeof value !== 'string') throw new Error('Expected a string or string array')
  if (value.startsWith('[')) {
    try { return words(JSON.parse(value)) } catch (cause) { throw new Error('Invalid JSON argument array', { cause }) }
  }
  if (value.includes('\0') || /[\r\n`]|\$\(/.test(value)) throw new Error('Editor arguments must be literal; shell substitutions require a build-system compilation database')
  const result = []
  let word = ''
  let quote = ''
  let started = false
  for (let index = 0; index < value.length; index++) {
    const character = value[index]
    if (character === '\\' && quote !== "'" && /[\s\\"']/.test(value[index + 1] || '')) {
      word += value[++index]
      started = true
    } else if (quote) {
      if (character === quote) quote = ''
      else word += character
    } else if (character === '"' || character === "'") {
      quote = character
      started = true
    } else if (/\s/.test(character)) {
      if (started) result.push(word)
      word = ''
      started = false
    } else {
      word += character
      started = true
    }
  }
  if (quote) throw new Error('Unterminated quote in editor arguments')
  if (started) result.push(word)
  return result
}

/**
 * Return the selected target identity; cross targets must be explicit.
 * @param {object} options
 * @param {object} config
 * @param {string} family
 * @returns {object}
 */
function target (options, config, family) {
  const host = { darwin: 'mac', win32: 'win', linux: 'linux' }[process.platform]
  const aliases = { desktop: host, macos: 'mac', darwin: 'mac', windows: 'win', win32: 'win', 'android-emulator': 'android' }
  const requested = options.platform || setting(config, `tooling.${family}.platform`, host)
  const platform = aliases[requested] || requested
  if (!['linux', 'mac', 'win', 'android', 'ios', 'ios-simulator'].includes(platform)) throw new Error(`Unsupported tooling platform: ${requested}`)
  const requestedArch = options.arch || setting(config, `tooling.${family}.arch`, platform === 'ios' || platform === 'android' ? 'arm64' : process.arch)
  const arch = { x64: 'x86_64', 'arm64-v8a': 'arm64', 'armeabi-v7a': 'armv7', ia32: 'x86' }[requestedArch] || requestedArch
  if (!['arm64', 'armv7', 'x86_64', 'x86'].includes(arch)) throw new Error(`Unsupported tooling architecture: ${requestedArch}`)
  return { platform, arch, buildType: options.prod ? 'release' : 'debug' }
}

/**
 * Return a hash including the content of explicit build inputs.
 * @param {object} identity
 * @param {string[]} inputs
 * @returns {string}
 */
function fingerprint (identity, inputs) {
  const evidence = [...new Set(inputs)].sort().map(filename => {
    try {
      const stat = fs.statSync(filename)
      return [filename, stat.isFile() ? crypto.createHash('sha256').update(fs.readFileSync(filename)).digest('hex') : 'directory']
    } catch (error) {
      if (error.code !== 'ENOENT') throw error
      return [filename, null]
    }
  })
  return crypto.createHash('sha256').update(JSON.stringify([identity, evidence])).digest('hex')
}

/**
 * Atomically publish a file without rewriting identical contents.
 * @param {string} filename
 * @param {string|object} value
 * @returns {void}
 */
function write (filename, value) {
  const content = typeof value === 'string' ? value : JSON.stringify(value, null, 2) + '\n'
  if (fs.existsSync(filename) && fs.readFileSync(filename, 'utf8') === content) return
  fs.mkdirSync(path.dirname(filename), { recursive: true })
  const temporary = `${filename}.${crypto.randomUUID()}`
  fs.writeFileSync(temporary, content, { mode: 0o600 })
  fs.renameSync(temporary, filename)
}

/**
 * Allocate a model identity without writing files. Tool providers add artifacts
 * and publish only when their own validation and preparation have succeeded.
 * @param {object} project
 * @param {object} options
 * @param {string} family
 * @param {object} identity
 * @param {string[]} inputs
 * @returns {object}
 */
function modelFor (project, options, family, identity, inputs = []) {
  const inputFiles = [...new Set([project.configuration, ...inputs, ...['.oro.env', '.ororc'].map(name => path.join(project.root, name))])]
  const environment = Object.fromEntries(['CC', 'CXX', 'SDKROOT', 'DEVELOPER_DIR', 'MACOSX_DEPLOYMENT_TARGET', 'IPHONEOS_VERSION_MIN', 'PKG_CONFIG_PATH', 'ANDROID_NDK_HOME', 'ANDROID_NDK_ROOT'].map(key => [key, project.env?.[key]]))
  const hash = fingerprint({ configuration: project.config, sdk: options.sdk, environment, family, ...identity }, inputFiles)
  const cache = path.resolve(project.root, setting(project.config, 'build.output', 'build'), '.oro-tooling', family)
  const pointer = path.join(cache, `${identity.platform || 'web'}-${identity.arch || 'any'}-${identity.buildType || 'default'}.json`)
  let generation = path.join(cache, options.refresh ? `${hash}-${crypto.randomUUID()}` : hash)
  if (!options.refresh && fs.existsSync(pointer)) {
    try {
      const previous = JSON.parse(fs.readFileSync(pointer, 'utf8'))
      const candidate = path.dirname(previous.modelFile || '')
      if (previous.fingerprint === hash && path.dirname(candidate) === cache && fs.existsSync(previous.modelFile)) generation = candidate
    } catch (error) {
      throw new Error(`Invalid ${family} model pointer ${pointer}. Run oroc tooling ${family} --refresh. ${error.message}`, { cause: error })
    }
  }
  return {
    schemaVersion: 1,
    language: family,
    projectRoot: project.root,
    configuration: project.configuration,
    ...identity,
    fingerprint: hash,
    generation,
    modelFile: path.join(generation, 'model.json'),
    pointer,
    freshnessInputs: inputFiles,
    prepared: Boolean(options.prepare),
    missingArtifacts: []
  }
}

/**
 * Publish a completed editor model and its current-generation pointer.
 * @param {object} model
 * @returns {object}
 */
function publish (model) {
  write(model.modelFile, model)
  write(model.pointer, { fingerprint: model.fingerprint, modelFile: model.modelFile })
  return model
}

/**
 * Inspect the previous model without preparing or writing any files.
 * @param {object} model
 * @returns {object}
 */
function freshness (model) {
  try {
    const pointer = JSON.parse(fs.readFileSync(model.pointer, 'utf8'))
    const previous = JSON.parse(fs.readFileSync(pointer.modelFile, 'utf8'))
    const missing = [...new Set([...(model.missingArtifacts || []), ...(previous.artifacts || []).filter(filename => !fs.existsSync(filename))])]
    return { status: pointer.fingerprint !== model.fingerprint ? 'stale' : missing.length ? 'missing-artifacts' : 'current', modelFile: pointer.modelFile, missing }
  } catch (error) {
    if (error.code === 'ENOENT') return { status: 'not-prepared' }
    return { status: 'invalid', message: error.message }
  }
}

module.exports = { setting, words, target, fingerprint, write, modelFor, publish, freshness }
