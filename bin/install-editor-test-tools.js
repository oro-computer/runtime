#!/usr/bin/env node

// CI-only pinned backend installation, kept separate from ordinary Oro startup.
import fs from 'node:fs'
import path from 'node:path'
import { createHash } from 'node:crypto'
import { execute } from '../src/cli/tooling/process.cjs'

const directory = process.argv[2]
if (!directory || !process.env.GITHUB_PATH) throw new Error('Usage in CI: install-editor-test-tools.js <temporary-directory>')
fs.mkdirSync(directory, { recursive: true })
const npmCli = process.env.npm_execpath || path.join(path.dirname(process.execPath), process.platform === 'win32' ? 'node_modules/npm/bin/npm-cli.js' : '../lib/node_modules/npm/bin/npm-cli.js')
await execute(process.execPath, [npmCli, 'install', '--prefix', directory, '--no-save', '--no-package-lock', '--no-audit', '--no-fund', 'typescript@5.9.3', 'typescript-language-server@6.0.2'], { log: true })
const paths = [path.join(directory, 'node_modules', '.bin')]
const platform = { linux: 'linux', darwin: 'mac', win32: 'windows' }[process.platform]
if (process.platform === 'linux' && process.arch === 'arm64') {
  await execute('sudo', ['apt-get', 'update'], { log: true })
  await execute('sudo', ['apt-get', 'install', '-y', 'clangd'], { log: true })
} else if (process.platform === 'darwin' && process.arch === 'arm64') {
  await execute('brew', ['install', 'llvm'], { log: true })
  paths.push(path.join((await execute('brew', ['--prefix', 'llvm'])).trim(), 'bin'))
} else {
  const archive = path.join(directory, 'clangd.zip')
  const clangd = await fetch(`https://github.com/clangd/clangd/releases/download/19.1.2/clangd-${platform}-19.1.2.zip`)
  if (!clangd.ok) throw new Error(`clangd download failed: ${clangd.status}`)
  fs.writeFileSync(archive, Buffer.from(await clangd.arrayBuffer()))
  if (process.platform === 'win32') await execute('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', 'Expand-Archive -LiteralPath $env:ORO_EDITOR_ARCHIVE -DestinationPath $env:ORO_EDITOR_DEST'], { log: true, env: { ...process.env, ORO_EDITOR_ARCHIVE: archive, ORO_EDITOR_DEST: directory } })
  else await execute('unzip', ['-q', archive, '-d', directory], { log: true })
  paths.push(path.join(directory, 'clangd_19.1.2', 'bin'))
}
if (process.platform === 'darwin') paths.push(path.dirname((await execute('xcrun', ['--find', 'sourcekit-lsp'])).trim()))
if (process.argv.includes('--android')) {
  // Read the same Gradle pin used by the SDK bootstrap, including artifact reuse.
  const bootstrap = fs.readFileSync(new URL('./android-functions.sh', import.meta.url), 'utf8')
  const gradleVersion = /declare GRADLE_VERSION="([^"]+)"/.exec(bootstrap)?.[1]
  const gradleDigest = /declare GRADLE_SHA256="([^"]+)"/.exec(bootstrap)?.[1]
  if (!gradleVersion || !gradleDigest) throw new Error('Missing SDK Gradle version or checksum')
  const gradle = await fetch(`https://services.gradle.org/distributions/gradle-${gradleVersion}-bin.zip`)
  if (!gradle.ok) throw new Error(`Gradle download failed: ${gradle.status}`)
  const bytes = Buffer.from(await gradle.arrayBuffer())
  if (createHash('sha256').update(bytes).digest('hex') !== gradleDigest) throw new Error('Gradle archive checksum mismatch')
  const gradleZip = path.join(directory, 'gradle.zip')
  fs.writeFileSync(gradleZip, bytes)
  await execute('unzip', ['-q', gradleZip, '-d', directory], { log: true })
  fs.appendFileSync(process.env.GITHUB_ENV, `GRADLE_HOME=${path.join(directory, `gradle-${gradleVersion}`)}\n`)
  const sdk = process.env.ANDROID_HOME
  if (!sdk) throw new Error('The Android qualification runner must provide ANDROID_HOME and accepted SDK licenses')
  const manager = ['cmdline-tools/23.0/bin/sdkmanager', 'cmdline-tools/latest/bin/sdkmanager'].map(file => path.join(sdk, file)).find(fs.existsSync)
  if (!manager) throw new Error('Install Android command-line tools on the qualification runner')
  const template = fs.readFileSync(new URL('../src/cli/templates.hh', import.meta.url), 'utf8')
  const compileSdk = /compileSdk (\d+)/.exec(template)?.[1]
  if (!compileSdk) throw new Error('Missing Android compile SDK in the selected generator')
  await execute(manager, ['--install', `platforms;android-${compileSdk}.0`, 'build-tools;36.0.0'], { log: true })
  const response = await fetch('https://github.com/fwcd/kotlin-language-server/releases/download/1.3.13/server.zip')
  if (!response.ok) throw new Error(`Kotlin backend download failed: ${response.status}`)
  const zip = path.join(directory, 'kotlin.zip')
  fs.writeFileSync(zip, Buffer.from(await response.arrayBuffer()))
  if (process.platform === 'win32') await execute('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', 'Expand-Archive -LiteralPath $env:ORO_EDITOR_ARCHIVE -DestinationPath $env:ORO_EDITOR_DEST'], { log: true, env: { ...process.env, ORO_EDITOR_ARCHIVE: zip, ORO_EDITOR_DEST: directory } })
  else await execute('unzip', ['-q', zip, '-d', directory], { log: true })
  paths.push(path.join(directory, 'server', 'bin'))
}
fs.appendFileSync(process.env.GITHUB_PATH, paths.join('\n') + '\n')
