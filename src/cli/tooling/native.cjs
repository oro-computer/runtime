'use strict'

const fs = require('node:fs')
const { readAndroidSdk } = require('../android-sdk.cjs')
const path = require('node:path')
const { execute } = require('./process.cjs')
const { markGeneration } = require('./cache.cjs')
const { resolveProgram } = require('./servers.cjs')
const { setting, words, target, modelFor, write, publish } = require('./model.cjs')

const sourceExtensions = new Set(['.c', '.cc', '.cpp', '.cxx', '.c++', '.m', '.mm'])

function flatten (value, prefix = '', result = {}) {
  for (const [key, item] of Object.entries(value)) {
    const name = prefix + key
    if (item && typeof item === 'object' && !Array.isArray(item)) flatten(item, name + '_', result)
    else result[name] = item
  }
  return result
}

function sourceFile (root, filename) {
  const absolute = path.resolve(root, filename)
  if (!fs.existsSync(absolute)) throw new Error(`Native source is missing: ${absolute}. Prepare its producer or select a build-system compilation database.`)
  if (!fs.statSync(absolute).isFile()) throw new Error(`Native source must be a file: ${absolute}`)
  return fs.realpathSync(absolute)
}

async function declarations (project, platform) {
  const config = flatten(project.config)
  const names = new Set()
  const scope = platform.startsWith('ios') ? 'ios' : platform
  for (const key of Object.keys(config)) {
    if (!key.startsWith('build_extensions_')) continue
    let suffix = key.slice('build_extensions_'.length)
    const prefix = ['android', 'ios', 'mac', 'win', 'linux'].find(name => suffix.startsWith(name + '_'))
    if (prefix && prefix !== scope) continue
    if (prefix) suffix = suffix.slice(prefix.length + 1)
    const name = suffix.split('_')[0]
    if (!['compiler', 'linker', 'abi'].includes(name)) names.add(name)
  }
  const units = []
  const inputs = []
  for (const name of names) {
    const base = `build_extensions_${name}`
    const scoped = `build_extensions_${scope}_${name}`
    const keys = [base, scoped, `${base}_${scope}`]
    let root = project.root
    let local = {}
    const location = config[base + '_source'] || config[scoped + '_source'] || config[base + '_path'] || keys.map(key => config[key]).find(value => typeof value === 'string' && fs.existsSync(path.resolve(root, value)) && fs.statSync(path.resolve(root, value)).isDirectory())
    if (location) {
      root = path.resolve(project.root, location)
      if (!fs.existsSync(root)) throw new Error(`Extension ${name} is not installed at ${root}. Install it before exporting an editor model; tooling does not clone repositories.`)
      const configuration = ['oro.toml', 'oro.ini'].map(file => path.join(root, file)).find(file => fs.existsSync(file))
      if (configuration) {
        inputs.push(configuration)
        local = flatten(JSON.parse(await execute(project.oroc, ['config', '--json', '--config', configuration], { cwd: root, env: project.env })))
      }
    }
    const opaque = ['configure_script', 'build_script'].find(key => config[`${base}_${key}`] || local[`extension_${key}`])
    if (opaque) throw new Error(`Extension ${name} uses ${opaque}. Select tooling.cpp.compilation_database from that build, or tooling.cpp.cmake with explicit prepare_targets; Oro cannot infer a script's generated compiler inputs.`)
    const sources = [...words(local.extension_sources), ...words(local[`extension_${scope}_sources`]), ...keys.flatMap(key => {
      const value = config[key]
      if (typeof value === 'string' && fs.existsSync(path.resolve(project.root, value)) && fs.statSync(path.resolve(project.root, value)).isDirectory()) return []
      return words(value)
    })]
    const flags = ['compiler_flags', ...(project.prod ? [] : ['compiler_debug_flags'])].flatMap(suffix => [
      config[`build_extensions_${suffix}`], config[`build_extensions_${scope}_${suffix}`],
      config[`${base}_${suffix}`], local[`extension_${suffix}`], config[`${base}_${scope}_${suffix}`], local[`extension_${scope}_${suffix}`]
    ]).flatMap(words)
    for (const source of sources) {
      if (!sourceExtensions.has(path.extname(source))) continue
      if (['wasm32', 'wasm32-wasi'].includes(config[base + '_target'] || local.extension_target)) continue
      units.push({ file: sourceFile(root, source), directory: root, flags, extension: name })
    }
  }
  if (scope === 'android') {
    for (const source of words(config.android_native_sources)) {
      if (sourceExtensions.has(path.extname(source))) units.push({ file: sourceFile(project.root, source), directory: project.root, flags: [], extension: null })
    }
  }
  return { units, inputs }
}

/**
 * Locate the actual Apple SDK and compiler selected by xcrun.
 * @param {object} identity
 * @param {object} project
 * @param {string} [compiler]
 * @returns {Promise<object>}
 */
async function appleToolchain (identity, project, compiler = 'clang') {
  if (process.platform !== 'darwin') throw new Error(`${identity.platform} tooling requires macOS with Xcode and its selected SDK installed`)
  const sdk = { mac: 'macosx', ios: 'iphoneos', 'ios-simulator': 'iphonesimulator' }[identity.platform]
  const xcrun = resolveProgram('xcrun', project.env)
  const run = async args => (await execute(xcrun, ['--sdk', sdk, ...args], { cwd: project.root, env: project.env, timeout: 30000 })).trim()
  const [executable, sysroot, sdkVersion] = await Promise.all([run(['--find', compiler]), run(['--show-sdk-path']), run(['--show-sdk-version'])])
  const minimum = setting(project.config, `tooling.${compiler === 'swiftc' ? 'swift' : 'cpp'}.deployment_target`, identity.platform === 'mac' ? project.env.MACOSX_DEPLOYMENT_TARGET || setting(project.config, 'mac.minimum_supported_version', '13.0.0') : project.env.IPHONEOS_VERSION_MIN || '15.0')
  if (!/^\d+(\.\d+){0,2}$/.test(minimum)) throw new Error(`Invalid Apple deployment target: ${minimum}`)
  const triple = `${identity.arch}-apple-${identity.platform === 'mac' ? 'macosx' : 'ios'}${minimum}${identity.platform === 'ios-simulator' ? '-simulator' : ''}`
  return { executable, sysroot, sdk, sdkVersion, triple, minimum }
}

async function nativeToolchain (identity, project) {
  if (['mac', 'ios', 'ios-simulator'].includes(identity.platform)) return appleToolchain(identity, project)
  if (identity.platform === 'android') {
    const sdk = await readAndroidSdk(project.oroc, { cwd: project.root, env: project.env })
    let ndk = setting(project.config, 'tooling.cpp.ndk', project.env.ANDROID_NDK_HOME || project.env.ANDROID_NDK_ROOT)
    if (!ndk) {
      const version = sdk.ndkVersion
      if (version && project.env.ANDROID_HOME) ndk = path.join(project.env.ANDROID_HOME, 'ndk', version)
    }
    if (!ndk) throw new Error('Select the NDK used by this Oro SDK with ANDROID_NDK_HOME or tooling.cpp.ndk; tooling never selects an arbitrary installed NDK version')
    ndk = fs.realpathSync(path.resolve(project.root, ndk))
    const host = { linux: 'linux-x86_64', darwin: 'darwin-x86_64', win32: 'windows-x86_64' }[process.platform]
    const prebuilt = path.join(ndk, 'toolchains', 'llvm', 'prebuilt', host)
    const api = String(sdk.nativeApi)
    if (!/^\d+$/.test(api)) throw new Error('The selected Oro CLI does not export its Android native API level. Rebuild/relink the CLI and SDK together, or import an existing native compilation database.')
    const triple = { arm64: 'aarch64-linux-android', armv7: 'armv7a-linux-androideabi', x86_64: 'x86_64-linux-android', x86: 'i686-linux-android' }[identity.arch] + api
    const executable = resolveProgram(path.join(prebuilt, 'bin', process.platform === 'win32' ? 'clang.exe' : 'clang'), project.env)
    return { executable, sysroot: path.join(prebuilt, 'sysroot'), triple, ndk, api }
  }
  const host = { linux: 'linux', win32: 'win' }[process.platform]
  if (identity.platform !== host) throw new Error(`Cross compilation for ${identity.platform} requires a build-system compilation database with its actual compiler and SDK`)
  const executable = resolveProgram(project.env.CC || 'clang', project.env)
  const triple = (await execute(executable, ['-dumpmachine'], { env: project.env, timeout: 10000 })).trim()
  const hostArch = process.arch === 'x64' ? 'x86_64' : process.arch
  if (identity.arch !== hostArch) throw new Error(`Architecture ${identity.arch} requires a build-system compilation database from the selected cross toolchain`)
  const compilerVersion = (await execute(executable, ['--version'], { env: project.env, timeout: 10000 })).trim()
  const cppExecutable = resolveProgram(project.env.CXX || 'clang++', project.env)
  const cppCompilerVersion = (await execute(cppExecutable, ['--version'], { env: project.env, timeout: 10000 })).trim()
  return { executable, triple, compilerVersion, cppExecutable, cppCompilerVersion }
}

function databaseEntries (filename) {
  const entries = JSON.parse(fs.readFileSync(filename, 'utf8'))
  if (!Array.isArray(entries) || entries.length > 100000) throw new Error('Compilation database must contain at most 100000 entries')
  return entries.filter(entry => typeof entry.file === 'string' && sourceExtensions.has(path.extname(entry.file))).map(entry => {
    if (typeof entry.file !== 'string' || typeof entry.directory !== 'string') throw new Error('Compilation database entries require file and directory')
    const directory = path.resolve(path.dirname(filename), entry.directory)
    const args = entry.arguments ? words(entry.arguments) : words(entry.command)
    if (!args.length) throw new Error(`Empty compile command for ${entry.file}`)
    return { file: sourceFile(directory, entry.file), directory, arguments: args }
  })
}

/**
 * Export a C-family compiler database from native declarations or a build provider.
 * @param {object} options
 * @param {object} project
 * @returns {Promise<object>}
 */
async function nativeModel (options, project) {
  const identity = target(options, project.config, 'cpp')
  project = { ...project, prod: options.prod }
  const database = setting(project.config, 'tooling.cpp.compilation_database')
  const cmake = setting(project.config, 'tooling.cpp.cmake')
  if (database && cmake) throw new Error('Choose one native provider: tooling.cpp.compilation_database or tooling.cpp.cmake')
  const declarationsResult = database || cmake ? { units: [], inputs: [] } : await declarations(project, identity.platform)
  const inputs = [__filename, ...declarationsResult.inputs]
  const sdk = options.sdk
  if (sdk) inputs.push(path.join(sdk, 'VERSION.txt'), path.join(sdk, 'include', 'oro', 'extension.h'))
  if (database) inputs.push(path.resolve(project.root, database))
  if (cmake) {
    inputs.push(path.resolve(project.root, cmake, 'CMakeLists.txt'))
    const toolchainFile = setting(project.config, 'tooling.cpp.toolchain')
    if (toolchainFile) inputs.push(path.resolve(project.root, toolchainFile))
    const host = target({}, {}, 'cpp')
    if ((identity.platform !== host.platform || identity.arch !== host.arch) && !toolchainFile) throw new Error('CMake cross targets require tooling.cpp.toolchain selecting the actual target and SDK')
  }
  const toolchain = database || cmake ? null : await nativeToolchain(identity, project)
  const runtimeVersion = (await execute(project.oroc, ['--version'], { env: project.env, timeout: 10000 })).trim()
  if (toolchain?.ndk) inputs.push(path.join(toolchain.ndk, 'source.properties'))
  const model = modelFor(project, options, 'cpp', { ...identity, runtimeVersion, provider: database ? 'compilation-database' : cmake ? 'cmake' : 'oro', toolchain, originalSources: declarationsResult.units.map(unit => unit.file) }, inputs)
  model.compileCommands = path.join(model.generation, 'compile_commands.json')
  let entries
  if (database) {
    entries = databaseEntries(path.resolve(project.root, database))
  } else if (cmake) {
    const program = resolveProgram('cmake', project.env)
    const source = path.resolve(project.root, cmake)
    const directory = path.join(model.generation, 'cmake')
    if (!options.inspect) {
      const configure = ['-S', source, '-B', directory, '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', `-DCMAKE_BUILD_TYPE=${options.prod ? 'Release' : 'Debug'}`]
      const toolchainFile = setting(project.config, 'tooling.cpp.toolchain')
      if (toolchainFile) configure.push(`-DCMAKE_TOOLCHAIN_FILE=${path.resolve(project.root, toolchainFile)}`)
      configure.push(...words(setting(project.config, 'tooling.cpp.configure_args')))
      if (options.offline) configure.push('-DFETCHCONTENT_FULLY_DISCONNECTED=ON', '-DFETCHCONTENT_UPDATES_DISCONNECTED=ON')
      markGeneration(model)
      await execute(program, configure, { cwd: project.root, env: project.env, log: true })
      const targets = words(setting(project.config, 'tooling.cpp.prepare_targets'))
      if (options.prepare && targets.length) await execute(program, ['--build', directory, '--target', ...targets], { cwd: project.root, env: project.env, log: true })
    }
    const filename = path.join(directory, 'compile_commands.json')
    if (!fs.existsSync(filename)) {
      if (options.inspect) return { ...model, originalSources: [], artifacts: [model.compileCommands], missingArtifacts: [filename] }
      throw new Error('CMake did not export compile_commands.json. Select a Makefile or Ninja generator in tooling.cpp.configure_args.')
    }
    entries = databaseEntries(filename)
  } else {
    if (!sdk || !fs.existsSync(path.join(sdk, 'include', 'oro', 'extension.h'))) throw new Error('Selected Oro SDK is missing include/oro/extension.h; install the native SDK before preparing editor tooling')
    let platformFlags = []
    if (identity.platform === 'linux') platformFlags = words((await execute(resolveProgram('pkg-config', project.env), ['--cflags', 'gtk+-3.0', 'webkit2gtk-4.1'], { env: project.env, timeout: 10000 })).trim())
    const androidFlags = words(setting(project.config, 'android.native_cflags'))
    const version = /^(\S+)\s+\(([^)]+)\)/.exec(runtimeVersion)
    entries = declarationsResult.units.map(unit => {
      const ext = path.extname(unit.file)
      const cpp = !['.c', '.m'].includes(ext)
      const apple = ['mac', 'ios', 'ios-simulator'].includes(identity.platform)
      const language = ext === '.mm' || (apple && cpp) ? 'objective-c++' : ext === '.m' || (apple && !cpp) ? 'objective-c' : cpp ? 'c++' : 'c'
      let compiler = toolchain.executable
      if (cpp && toolchain.cppExecutable) compiler = toolchain.cppExecutable
      const args = [compiler, '-x', language, ...(cpp ? ['-std=c++20'] : []), '-fvisibility=hidden',
        `-DIOS=${identity.platform.startsWith('ios') ? 1 : 0}`, `-DANDROID=${identity.platform === 'android' ? 1 : 0}`,
        `-DDEBUG=${options.prod ? 0 : 1}`, '-I', path.join(sdk, 'include'), '-I', path.join(sdk, 'src'),
        '-DHOST="http://localhost"', '-DPORT=0',
        ...(version ? [`-DORO_RUNTIME_VERSION=${version[1]}`, `-DORO_RUNTIME_VERSION_HASH=${version[2]}`] : []), ...platformFlags]
      if (unit.extension) args.push('-DORO_RUNTIME_EXTENSION=1')
      if (identity.platform !== 'win') args.push('-fPIC')
      else args.push('-DWIN32', '-DWIN32_LEAN_AND_MEAN', options.prod ? '-fms-runtime-lib=dll' : '-fms-runtime-lib=dll_dbg')
      if (toolchain.sysroot) args.push(apple ? '-isysroot' : '--sysroot', toolchain.sysroot, '-target', toolchain.triple)
      if (apple) {
        const frameworks = ['UniformTypeIdentifiers', 'CoreLocation', 'Network', 'UserNotifications', 'Metal', 'Accelerate', 'WebKit', 'OSLog', ...(identity.platform === 'mac' ? ['Cocoa'] : ['UIKit', 'CoreBluetooth', 'QuartzCore'])]
        args.push(...frameworks.flatMap(framework => ['-framework', framework]))
      }
      if (identity.platform === 'android') args.push('-pthread', ...(options.prod ? [] : ['-O0', '-g']), ...androidFlags)
      if (identity.platform === 'win') args.push('-D_MT', '-D_DLL', '-Wno-nonportable-include-path', ...(options.prod ? [] : ['-D_DEBUG']), ...(cpp ? ['-stdlib=libstdc++'] : []))
      if (cpp && ['linux', 'win'].includes(identity.platform)) args.push('-Wno-unused-command-line-argument')
      args.push(...unit.flags, '-c', unit.file)
      return { file: unit.file, directory: unit.directory, arguments: args }
    })
  }
  const declaredScope = words(setting(project.config, 'tooling.cpp.sources')).map(file => sourceFile(project.root, file))
  if (declaredScope.length) {
    const available = new Set(entries.map(entry => entry.file))
    for (const file of declaredScope) if (!available.has(file)) throw new Error(`No compiler command for declared source ${file}`)
    entries = entries.filter(entry => declaredScope.includes(entry.file))
  }
  if (!entries.length) throw new Error('No native compilation units declared. Declare build.extensions sources, android.native_sources, or a tooling.cpp build provider in oro.toml.')
  model.originalSources = [...new Set(entries.map(entry => entry.file))]
  model.headerRoots = [...new Set(entries.flatMap(entry => {
    const roots = []
    const args = entry.arguments || words(entry.command)
    for (let index = 0; index < args.length; index++) {
      const flag = args[index]
      const include = ['-I', '-iquote', '-isystem'].includes(flag) ? args[++index] : /^-I(.+)/.exec(flag)?.[1]
      if (!include) continue
      const directory = path.resolve(entry.directory, include)
      const relative = path.relative(project.root, directory)
      if (relative && !relative.startsWith('..') && !path.isAbsolute(relative)) roots.push(directory)
    }
    return roots
  }))]
  model.dependencyRoots = [...model.headerRoots, ...(sdk && fs.existsSync(path.join(sdk, 'include', 'oro')) ? [fs.realpathSync(path.join(sdk, 'include', 'oro'))] : [])]
  model.artifacts = [model.compileCommands, ...model.originalSources, ...(toolchain ? [toolchain.executable, path.join(sdk, 'include', 'oro', 'extension.h')] : [])]
  model.freshnessInputs.push(...model.originalSources.map(file => path.dirname(file)))
  model.commands = entries
  model.preparation = model.provider === 'oro' ? 'Uses installed SDK headers; no linking, packaging or signing is needed.' : 'Uses the selected build provider; only configured preparation targets are built.'
  if (options.inspect) return model
  markGeneration(model)
  write(model.compileCommands, entries)
  return publish(model)
}

module.exports = { nativeModel, appleToolchain }
