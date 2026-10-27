#!/usr/bin/env node

const fs = require('node:fs/promises')
const path = require('node:path')
const os = require('node:os')
const { X509Certificate } = require('node:crypto')
const { spawn } = require('node:child_process')

function origin (value) {
  const url = new URL(value)
  if (url.protocol !== 'https:' || url.username || url.password || url.pathname !== '/' || url.search || url.hash || value.length > 850 || url.port === '0' || ['0.0.0.0', '[::]'].includes(url.hostname)) {
    throw new Error('--server must be an HTTPS origin without credentials, path or query')
  }
  return url.origin
}

async function selectPlatform (sdk) {
  const platforms = path.join(sdk, 'platforms')
  const names = (await fs.readdir(platforms)).filter(name => /^android-\d+(?:\.0)?$/.test(name))
    .sort((left, right) => Number(right.slice(8)) - Number(left.slice(8)))
  for (const name of names) {
    const level = Number(name.slice(8))
    if (level < 34) continue
    try {
      const properties = await fs.readFile(path.join(platforms, name, 'source.properties'), 'utf8')
      const actual = /^AndroidVersion.ApiLevel=(.+)$/m.exec(properties)?.[1].trim()
      const jar = await fs.stat(path.join(platforms, name, 'android.jar'))
      if (Number(actual) === level && jar.isFile() && jar.size > 0) return level
    } catch (error) { if (error.code !== 'ENOENT') throw error }
  }
  throw new Error('Install an Android SDK platform 34 or newer using oroc setup --platform=android')
}

function parseOptions (args) {
  const values = new Set(['android-home', 'gradle-home', 'java-home', 'data-dir', 'output', 'device', 'server', 'ca-cert'])
  const switches = new Set(['install', 'offline'])
  const config = process.env.ORO_CONFIG_HOME || (process.platform === 'win32'
    ? path.join(process.env.LOCALAPPDATA || os.homedir(), 'oro')
    : path.join(process.env.XDG_DATA_HOME || path.join(os.homedir(), '.local', 'share'), 'oro'))
  const options = {
    'android-home': process.env.ANDROID_HOME || process.env.ANDROID_SDK_ROOT,
    'gradle-home': process.env.GRADLE_HOME,
    'java-home': process.env.JAVA_HOME,
    'data-dir': path.join(config, 'push', 'distributor'),
    device: process.env.ANDROID_SERIAL
  }
  const seen = new Set()
  for (let index = 0; index < args.length; index++) {
    const match = /^--([a-z-]+)(?:=(.*))?$/.exec(args[index])
    if (!match || (!values.has(match[1]) && !switches.has(match[1]))) throw new Error(`Unknown push distributor option: ${args[index]}`)
    const [, name, inline] = match
    if (seen.has(name)) throw new Error(`Duplicate --${name}`)
    seen.add(name)
    if (switches.has(name)) {
      if (inline !== undefined) throw new Error(`--${name} takes no value`)
      options[name] = true
    } else {
      const value = inline === undefined ? args[++index] : inline
      if (!value || value.startsWith('--')) throw new Error(`--${name} requires a value`)
      options[name] = value
    }
  }
  if (options.server) options.server = origin(options.server)
  if (options.install && !options.device) throw new Error('--install requires --device=<serial> or ANDROID_SERIAL')
  for (const name of ['android-home', 'gradle-home', 'java-home']) {
    if (!options[name]) throw new Error(`Configure ${name.toUpperCase().replace('-', '_')} or --${name}; run oroc setup --platform=android first`)
  }
  for (const name of ['android-home', 'gradle-home', 'java-home', 'data-dir', 'ca-cert', 'output']) {
    if (options[name]) options[name] = path.resolve(options[name])
  }
  options.output ||= path.join(options['data-dir'], 'oro-push-dev.apk')
  return options
}

function run (executable, args, options, signal) {
  signal.throwIfAborted()
  return new Promise((resolve, reject) => {
    const child = spawn(executable, args, { ...options, detached: process.platform !== 'win32', stdio: 'inherit', windowsHide: true })
    let timer
    function kill (name) {
      if (!child.pid) return
      try {
        if (process.platform === 'win32') child.kill(name)
        else process.kill(-child.pid, name)
      } catch (error) { if (error.code !== 'ESRCH') throw error }
    }
    function stop () {
      kill('SIGTERM')
      timer = setTimeout(() => kill('SIGKILL'), 5000)
      timer.unref()
    }
    signal.addEventListener('abort', stop, { once: true })
    child.once('error', reject)
    child.once('close', (code, termination) => {
      signal.removeEventListener('abort', stop)
      clearTimeout(timer)
      if (code === 0) resolve()
      else reject(new Error(`${path.basename(executable)} failed (${termination || code})`))
    })
    if (signal.aborted) stop()
  })
}

async function build (options, signal) {
  const extension = process.platform === 'win32' ? '.exe' : ''
  const java = path.join(options['java-home'], 'bin', `java${extension}`)
  const keytool = path.join(options['java-home'], 'bin', `keytool${extension}`)
  const sdk = options['android-home']
  const gradle = path.join(options['gradle-home'], 'lib')
  const launcher = (await fs.readdir(gradle)).find(name => /^gradle-gradle-cli-main-[\d.]+\.jar$/.test(name))
  if (!launcher) throw new Error('--gradle-home must contain a Gradle 9 installation')
  const platform = await selectPlatform(sdk)
  const env = { ...process.env, JAVA_HOME: options['java-home'], ANDROID_HOME: sdk, ANDROID_SDK_ROOT: sdk }
  if (options.install) {
    // Fail before building or installing when the requested device is not authorized.
    const adb = path.join(sdk, 'platform-tools', `adb${extension}`)
    await run(adb, ['-s', options.device, 'get-state'], { env }, signal)
  }
  let certificate
  if (options['ca-cert']) {
    const pem = await fs.readFile(options['ca-cert'], 'utf8')
    const certificates = pem.match(/-----BEGIN CERTIFICATE-----[\s\S]*?-----END CERTIFICATE-----/g)
    if (!certificates?.length) throw new Error('--ca-cert must contain PEM X.509 certificates')
    certificate = certificates.map(value => new X509Certificate(value).toString()).join('\n')
  }
  await fs.mkdir(options['data-dir'], { recursive: true, mode: 0o700 })
  const directory = await fs.mkdtemp(path.join(options['data-dir'], 'build-'))
  try {
    const source = options.sourceDirectory || path.join(__dirname, 'push-distributor')
    await fs.mkdir(path.join(directory, 'kotlin'))
    await fs.mkdir(path.join(directory, 'assets'))
    for (const name of await fs.readdir(source)) {
      if (name.endsWith('.kt')) await fs.copyFile(path.join(source, name), path.join(directory, 'kotlin', name))
      else if (name.endsWith('.gradle') || name === 'AndroidManifest.xml') await fs.copyFile(path.join(source, name), path.join(directory, name))
    }
    if (certificate) await fs.writeFile(path.join(directory, 'assets', 'development-ca.pem'), certificate)
    if (options.server) await fs.writeFile(path.join(directory, 'assets', 'development-server.txt'), options.server)
    const keystore = path.join(options['data-dir'], 'development.jks')
    try { await fs.access(keystore) } catch (error) {
      if (error.code !== 'ENOENT') throw error
      await run(keytool, ['-genkeypair', '-keystore', keystore, '-storetype', 'JKS', '-storepass', 'android', '-keypass', 'android',
        '-alias', 'oro-push-dev', '-dname', 'CN=Oro Push Development', '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000'], { env }, signal)
    }
    console.error('oroc push distributor: building the Kotlin development APK')
    const args = ['-classpath', path.join(gradle, launcher), 'org.gradle.launcher.GradleMain', '--no-daemon', '--console=plain',
      `-PoroPushKeystore=${keystore}`, `-PoroPushCompileSdk=${platform}`, 'assembleDebug']
    if (options.offline) args.push('--offline')
    await run(java, args, { env, cwd: directory }, signal)
    const apk = path.join(directory, 'build', 'outputs', 'apk', 'debug', 'oro-push-development-distributor-debug.apk')
    await fs.mkdir(path.dirname(options.output), { recursive: true })
    await fs.copyFile(apk, options.output)
    console.error(`oroc push distributor: APK: ${options.output}`)
    if (options.install) {
      const adb = path.join(sdk, 'platform-tools', `adb${extension}`)
      await run(adb, ['-s', options.device, 'install', '-r', options.output], { env }, signal)
      await run(adb, ['-s', options.device, 'shell', 'am', 'start', '-n', 'dev.oro.push/.SettingsActivity'], { env }, signal)
      console.error('oroc push distributor: review the HTTPS server and tap Save and start in Oro Push Dev')
    }
    return options.output
  } finally {
    await fs.rm(directory, { recursive: true, force: true })
  }
}

async function main () {
  const controller = new AbortController()
  let interrupted = 0
  function stop (signal) {
    if (interrupted) return
    interrupted = signal === 'SIGINT' ? 130 : 143
    controller.abort(new Error('Distributor build interrupted'))
  }
  process.on('SIGINT', stop)
  process.on('SIGTERM', stop)
  try {
    if (Number(process.versions.node.split('.')[0]) < 22) throw new Error('Node.js 22 or later is required')
    await build(parseOptions(process.argv.slice(2)), controller.signal)
  } catch (error) {
    if (!interrupted) console.error(`oroc push distributor: ${error.message}`)
    process.exitCode = interrupted || 1
  } finally {
    process.removeListener('SIGINT', stop)
    process.removeListener('SIGTERM', stop)
  }
}

module.exports = { origin, parseOptions, selectPlatform, build }
if (require.main === module) main()
