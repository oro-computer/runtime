import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)
const root = path.resolve(__dirname, '..')

function runCli (command, args, options = {}) {
  return spawnSync(command, args, {
    encoding: 'utf8',
    shell: process.platform === 'win32',
    ...options
  })
}

const runOroc = (args, options = {}) => runCli('oroc', args, options)
const MINIMAL_CONFIG = '[build]\nname = dotfiles-test\n'

function assert (cond, msg) {
  if (!cond) {
    console.error('Assertion failed:', msg)
    process.exit(1)
  }
}

// 1) oroc --prefix
{
  const { status, stdout } = runOroc(['--prefix'])
  assert(status === 0, 'oroc --prefix should exit 0')
  assert((stdout || '').trim().length > 0, 'oroc --prefix should print a path')
}

// 2) oroc --help includes usage/subcommands
{
  const { status, stdout } = runOroc(['--help'])
  assert(status === 0, 'oroc --help should exit 0')
  const out = (stdout || '').toLowerCase()
  assert(
    out.includes('usage') && out.includes('subcommands'),
    'oroc --help should include usage and subcommands'
  )
}

// 3) oroc -V --help (global short verbose)
{
  const { status } = runOroc(['-V', '--help'])
  assert(status === 0, 'oroc -V --help should exit 0')
}

// 4) oroc --unknown should fail with code 1
{
  const { status } = runOroc(['--definitely-unknown'])
  assert(status === 1, 'unknown top-level option should exit 1')
}

// 5) oroc print-build-dir with -V from test project
{
  const cwd = root
  const { status, stdout } = runOroc(
    ['print-build-dir', '--platform=linux', '-V'],
    { cwd }
  )
  assert(status === 0, 'print-build-dir should exit 0')
  assert(
    (stdout || '').trim().length > 0,
    'print-build-dir should print a path'
  )
}

// 6) oroc -D --help (global short debug)
{
  const { status } = runOroc(['-D', '--help'])
  assert(status === 0, 'oroc -D --help should exit 0')
}

// 7) Dotfile env injection (.ororc)
{
  const projectRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-dotfiles-'))
  try {
    const oroConfig = path.join(projectRoot, 'oro.ini')
    fs.writeFileSync(oroConfig, MINIMAL_CONFIG, 'utf8')
    const preferredRc = path.join(projectRoot, '.ororc')
    fs.writeFileSync(preferredRc, 'env_RC_SOURCE = oro\n', 'utf8')

    const preferredResult = runOroc(['env'], { cwd: projectRoot })
    assert(preferredResult.status === 0, 'oroc env with .ororc should exit 0')
    assert(
      preferredResult.stdout.includes('RC_SOURCE=oro'),
      '.ororc should set env vars'
    )
  } finally {
    fs.rmSync(projectRoot, { recursive: true, force: true })
  }
}

// Device/install aliases share help, argument parsing, and Android targeting.
for (const [alias, command] of [['devices', 'list-devices'], ['install', 'install-app']]) {
  const canonical = runOroc([command, '--help'])
  const result = runOroc([alias, '--help'])
  assert(result.status === 0, `${alias} --help should exit 0`)
  assert(result.stdout === canonical.stdout, `${alias} should show ${command} help`)
}

// Use a fake SDK so connected phones/emulators and installation stay deterministic.
if (process.platform !== 'win32') {
  const project = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-cli-devices-'))
  const sdk = path.join(project, 'sdk')
  const log = path.join(project, 'adb.jsonl')
  fs.mkdirSync(path.join(sdk, 'platform-tools'), { recursive: true })
  fs.writeFileSync(path.join(project, 'oro.ini'), '[build]\nname = devices-test\noutput = out\n[meta]\nbundle_identifier = computer.oro.devices\nversion = 1.0.0\n')
  fs.writeFileSync(path.join(sdk, 'platform-tools/adb'), `#!/usr/bin/env node
const fs = require('node:fs')
const args = process.argv.slice(2)
fs.appendFileSync(process.env.ORO_TEST_ADB_LOG, JSON.stringify(args) + '\\n')
if (args.includes('devices')) {
  process.stdout.write(process.env.ORO_TEST_ADB_DEVICES)
  process.exit(Number(process.env.ORO_TEST_ADB_EXIT || 0))
}
if (args.includes('--version')) process.stdout.write('Android Debug Bridge version 1.0.41\\n')
`, { mode: 0o755 })

  for (const target of ['android', 'android-emulator']) {
    const apk = path.join(project, 'out', target, 'app/build/outputs/apk/dev/debug/app-dev-debug.apk')
    fs.mkdirSync(path.dirname(apk), { recursive: true })
    fs.writeFileSync(apk, '')
  }

  const inventory = 'List of devices attached\r\nunauthorized-phone\tunauthorized\r\noffline-emulator\toffline\r\nphone-first\tdevice\r\nemulator-5554\tdevice\r\nemulator-5556\tdevice\r\n'
  const env = {
    ...process.env,
    ANDROID_HOME: sdk,
    ANDROID_SERIAL: '',
    ORO_TEST_ADB_DEVICES: inventory,
    ORO_TEST_ADB_LOG: log
  }
  for (const command of ['devices', 'list-devices']) {
    const result = runOroc([command, '--platform=android', '--json'], { cwd: project, env })
    assert(result.status === 0, `${command} should list Android devices`)
    const devices = JSON.parse(result.stdout)
    assert(devices.map(device => device.id).join(',') === 'phone-first,emulator-5554,emulator-5556', 'inventory should preserve order and omit devices that are not ready')
  }

  for (const command of ['install', 'install-app', 'run']) {
    for (const scenario of [
      { target: 'android', expected: 'phone-first' },
      { target: 'android', inventory: 'List of devices attached\nemulator-5554\tdevice\nphone-second\tdevice\n', expected: 'emulator-5554' },
      { target: 'android-emulator', expected: 'emulator-5554' },
      { target: 'android', serial: 'emulator-5556', expected: 'emulator-5556' },
      { target: 'android', device: 'emulator-5554', serial: 'phone-first', expected: 'emulator-5554' }
    ]) {
      fs.writeFileSync(log, '')
      const args = [command, `--platform=${scenario.target}`]
      if (scenario.device) args.push('--device', scenario.device)
      const result = runOroc(args, {
        cwd: project,
        env: { ...env, ANDROID_SERIAL: scenario.serial || '', ORO_TEST_ADB_DEVICES: scenario.inventory || inventory }
      })
      assert(result.status === 0, `${args.join(' ')} should succeed: ${result.stderr}`)
      const calls = fs.readFileSync(log, 'utf8').trim().split('\n').map(line => JSON.parse(line))
      const installs = calls.filter(args => args.includes('install'))
      assert(installs.length === 1, `${command} should install once`)
      assert(installs[0][0] === '-s' && installs[0][1] === scenario.expected, `${command} should install on ${scenario.expected}`)
      if (command === 'run') {
        const launches = calls.filter(args => args.includes('shell'))
        assert(launches.length === 1 && launches[0][0] === '-s' && launches[0][1] === scenario.expected, 'launch should use the installation device')
      }
    }
  }

  for (const failure of [
    { ORO_TEST_ADB_DEVICES: 'List of devices attached\nphone\tunauthorized\n', ORO_TEST_ADB_EXIT: '0' },
    { ORO_TEST_ADB_DEVICES: 'adb discovery failed\n', ORO_TEST_ADB_EXIT: '1' }
  ]) {
    for (const command of ['install', 'run']) {
      fs.writeFileSync(log, '')
      const result = runOroc([command, '--platform=android'], { cwd: project, env: { ...env, ...failure } })
      assert(result.status !== 0, `${command} should fail if discovery fails or no ready devices exist`)
      assert(!fs.readFileSync(log, 'utf8').includes('"install"'), 'failed discovery should not install')
    }
  }
}

console.log('cli tests passed')
