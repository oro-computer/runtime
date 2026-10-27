#!/usr/bin/env node

// Linux CI fixtures only. These JARs are never application or SDK dependencies.
import fs from 'node:fs'
import path from 'node:path'
import { createHash, randomUUID } from 'node:crypto'
import { Readable, Transform } from 'node:stream'
import { pipeline } from 'node:stream/promises'
import { spawnSync } from 'node:child_process'

const help = 'Usage: node bin/install-native-test-tools.js <writable-test-directory>\nInstalls checksum-pinned Kotlin/JVM test fixtures and writes GitHub environment paths.\n'
if (process.argv.includes('--help')) { process.stdout.write(help); process.exit(0) }
if (process.platform !== 'linux' || process.argv.length !== 3) throw new Error(help)
const root = path.resolve(process.argv[2])
if (/[\r\n]/.test(root)) throw new Error('Test directory must not contain a newline')
const artifacts = JSON.parse(fs.readFileSync(new URL('../test/fixtures/android-runtime/host-tools.json', import.meta.url), 'utf8'))
const library = path.join(root, 'lib')
fs.mkdirSync(library, { recursive: true })

async function digest (filename) {
  const hash = createHash('sha256')
  for await (const chunk of fs.createReadStream(filename)) hash.update(chunk)
  return hash.digest('hex')
}

for (const artifact of artifacts) {
  const filename = path.join(library, artifact.name === 'kotlin-compiler-embeddable' ? 'kotlin-compiler.jar' : artifact.name + '.jar')
  if (!fs.existsSync(filename)) {
    const response = await fetch(artifact.url, { signal: AbortSignal.timeout(90000) })
    if (!response.ok || !response.body) throw new Error(`Fixture download failed: ${artifact.name}, HTTP ${response.status}`)
    const temporary = filename + '.' + randomUUID() + '.partial'
    let size = 0
    const bounded = new Transform({
      transform (chunk, encoding, callback) {
        size += chunk.length
        callback(size > 100 * 1024 * 1024 ? new Error('Fixture exceeds 100 MiB') : null, chunk)
      }
    })
    await pipeline(Readable.fromWeb(response.body), bounded, fs.createWriteStream(temporary, { flags: 'wx', mode: 0o600 }))
    if (await digest(temporary) !== artifact.sha256) throw new Error(`Fixture SHA-256 mismatch: ${artifact.name}`)
    fs.renameSync(temporary, filename)
  }
  if (!fs.lstatSync(filename).isFile() || await digest(filename) !== artifact.sha256) throw new Error(`Fixture SHA-256 mismatch: ${artifact.name}`)
}

const compiler = artifacts.slice(0, 7).map(artifact => path.join(library, artifact.name === 'kotlin-compiler-embeddable' ? 'kotlin-compiler.jar' : artifact.name + '.jar')).join(path.delimiter)
const stdlib = path.join(library, 'kotlin-stdlib.jar')
const binary = path.join(root, 'bin')
fs.mkdirSync(binary, { recursive: true })
const launcher = path.join(binary, 'kotlinc.cjs')
fs.writeFileSync(launcher, `const { spawnSync } = require('node:child_process')
const path = require('node:path')
const home = ${JSON.stringify(root)}
const stdlib = ${JSON.stringify(stdlib)}
const args = process.argv.slice(2)
const index = args.findIndex(value => value === '-classpath' || value === '-cp')
if (index >= 0 && index + 1 < args.length) args[index + 1] += path.delimiter + stdlib
else args.push('-classpath', stdlib)
const java = process.env.JAVA_HOME ? path.join(process.env.JAVA_HOME, 'bin', 'java') : 'java'
const child = spawnSync(java, ['-Dkotlin.home=' + home, '-cp', ${JSON.stringify(compiler)}, 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler', '-kotlin-home', home, ...args], { stdio: 'inherit' })
if (child.error) { process.stderr.write(child.error.message + '\\n'); process.exit(1) }
process.exit(child.status === null ? 1 : child.status)
`)
const quote = value => "'" + value.replaceAll("'", "'\\''") + "'"
fs.writeFileSync(path.join(binary, 'kotlinc'), `#!/bin/sh\nexec ${quote(process.execPath)} ${quote(launcher)} "$@"\n`, { mode: 0o700 })
const compiledVersion = spawnSync(path.join(binary, 'kotlinc'), ['-version'], { encoding: 'utf8', timeout: 30000 })
if (compiledVersion.status !== 0 || !/kotlinc-jvm 2\.2\.10\b/.test(compiledVersion.stdout + compiledVersion.stderr)) throw new Error('Pinned Kotlin compiler is unavailable; install JDK 21 before running this helper')
const environment = {
  ORO_KOTLIN_COMPILER_CP: compiler,
  ORO_KOTLIN_STDLIB: stdlib,
  ORO_TEST_JSON_JAR: path.join(library, 'json.jar'),
  ORO_TEST_SQLITE_JAR: path.join(library, 'sqlite-jdbc.jar'),
  ORO_TEST_SLF4J_JAR: path.join(library, 'slf4j-api.jar'),
  ORO_TEST_XML_JAR: path.join(library, 'kxml2.jar')
}
const sdk = process.env.ANDROID_HOME || process.env.ANDROID_SDK_ROOT
if (process.env.ORO_TEST_ANDROID_JAR) {
  if (!fs.statSync(process.env.ORO_TEST_ANDROID_JAR).isFile()) throw new Error('ORO_TEST_ANDROID_JAR must select an Android SDK JAR')
  environment.ORO_TEST_ANDROID_JAR = path.resolve(process.env.ORO_TEST_ANDROID_JAR)
} else if (sdk && fs.existsSync(path.join(sdk, 'platforms'))) {
  const platforms = fs.readdirSync(path.join(sdk, 'platforms')).filter(name => /^android-\d+(?:\.\d+)?$/.test(name))
    .sort((left, right) => Number.parseFloat(right.slice(8)) - Number.parseFloat(left.slice(8)))
  for (const platform of platforms) {
    const jar = path.join(sdk, 'platforms', platform, 'android.jar')
    if (fs.existsSync(jar)) { environment.ORO_TEST_ANDROID_JAR = jar; break }
  }
}
if (process.env.GITHUB_PATH) fs.appendFileSync(process.env.GITHUB_PATH, binary + '\n')
if (process.env.GITHUB_ENV) fs.appendFileSync(process.env.GITHUB_ENV, Object.entries(environment).map(([key, value]) => `${key}=${value}\n`).join(''))
fs.writeFileSync(path.join(root, 'environment.json'), JSON.stringify({ ...environment, PATH: binary + path.delimiter + process.env.PATH }, null, 2) + '\n')
process.stdout.write(JSON.stringify({ directory: root, artifactsVerified: artifacts.length, ...environment }, null, 2) + '\n')
