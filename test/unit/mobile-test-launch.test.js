import test from 'node:test'
import assert from 'node:assert/strict'
import { copyFileSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

function prepareHarness (platform, failure = '') {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-mobile-launch-'))
  const root = path.join(directory, 'test')
  const scripts = path.join(root, 'scripts')
  const bin = path.join(directory, 'bin')
  const container = path.join(directory, 'simulator data')
  const log = path.join(directory, 'commands.jsonl')
  mkdirSync(scripts, { recursive: true })
  mkdirSync(bin)
  mkdirSync(path.join(root, 'fixtures'))
  writeFileSync(path.join(root, 'fixtures', 'ready.txt'), 'fixture ready')
  writeFileSync(path.join(directory, 'package.json'), '{"type":"module"}')
  for (const filename of ['oroc-path.js', `test-${platform}.js`]) {
    copyFileSync(new URL(`../scripts/${filename}`, import.meta.url), path.join(scripts, filename))
  }
  const mock = `#!/usr/bin/env node
import { appendFileSync, existsSync, readFileSync } from 'node:fs'
import path from 'node:path'
const args = process.argv.slice(2)
const command = path.basename(process.argv[1])
appendFileSync(process.env.ORO_MOCK_LOG, JSON.stringify({ command, args }) + '\\n')
const step = command === 'oroc' ? 'build' : args[1]
if (step === process.env.ORO_MOCK_FAILURE) process.exit(7)
if (command === 'xcrun' && step === 'get_app_container') {
  console.log(process.env.ORO_MOCK_FAILURE === 'empty-container' ? '' : process.env.ORO_MOCK_CONTAINER)
}
if (command === 'xcrun' && step === 'launch') {
  const fixture = path.join(process.env.ORO_MOCK_CONTAINER, 'tmp', 'oro-test-fixtures', 'ready.txt')
  if (!existsSync(fixture) || readFileSync(fixture, 'utf8') !== 'fixture ready') process.exit(9)
  if (!process.env.SIMCTL_CHILD_ORO_CLI_PID) process.exit(10)
  if (process.env.ORO_MOCK_FAILURE === 'early-exit') process.exit(0)
  const pid = Number(process.env.SIMCTL_CHILD_ORO_CLI_PID)
  process.kill(pid, 'SIGUSR1')
  process.kill(pid, process.env.ORO_MOCK_FAILURE === 'signal-failure' ? 'SIGTERM' : 'SIGUSR2')
  setInterval(() => {}, 1000)
}
`
  for (const filename of ['oroc', 'xcrun', 'adb', 'mock-shell']) {
    writeFileSync(path.join(bin, filename), mock, { mode: 0o755 })
  }
  return {
    root,
    log,
    script: path.join(scripts, `test-${platform}.js`),
    env: {
      ...process.env,
      PATH: `${bin}${path.delimiter}${process.env.PATH}`,
      ORO_BIN: path.join(bin, 'oroc'),
      ANDROID_HOME: '',
      SHELL: path.join(bin, 'mock-shell'),
      ORO_MOCK_LOG: log,
      ORO_MOCK_CONTAINER: container,
      ORO_MOCK_FAILURE: failure
    }
  }
}

function runHarness (harness) {
  const result = spawnSync(process.execPath, [harness.script], {
    cwd: harness.root,
    env: harness.env,
    encoding: 'utf8',
    timeout: 10000
  })
  const commands = readFileSync(harness.log, 'utf8').trim().split('\n').map(line => JSON.parse(line))
  return { result, commands }
}

for (const simulator of ['E63A4C69-8ADF-4037-A5EE-AE47A98E069B', '']) {
  test(`iOS tests prepare fixtures before launching on ${simulator || 'booted'}`, () => {
    const harness = prepareHarness('ios-simulator')
    harness.env.ORO_IOS_SIMULATOR_UDID = simulator
    const { result, commands } = runHarness(harness)
    assert.equal(result.status, 0, result.stderr)
    assert.deepEqual(commands.map(({ command, args }) => command === 'oroc' ? 'build' : args[1]),
      ['build', 'install', 'get_app_container', 'launch'])
    assert.ok(!commands[0].args.includes('-r'), 'building must not launch the app before fixtures are ready')
    for (const { args } of commands.slice(1)) {
      assert.ok(args.includes(simulator || 'booted'), 'all simulator commands must address the selected device')
    }
    assert.ok(commands[1].args.at(-1).endsWith('oro-runtime-javascript-tests-dev.app'))
  })
}

for (const failure of ['build', 'install', 'get_app_container', 'empty-container', 'launch', 'signal-failure', 'early-exit']) {
  test(`iOS tests report ${failure} failure`, () => {
    const harness = prepareHarness('ios-simulator', failure)
    const { result, commands } = runHarness(harness)
    assert.notEqual(result.status, 0)
    assert.equal(result.signal, null, result.stderr)
    if (!['launch', 'signal-failure', 'early-exit'].includes(failure)) {
      assert.ok(!commands.some(({ args }) => args[1] === 'launch'))
    }
  })
}

for (const ci of ['1', '']) {
  test(`Android ${ci ? 'CI' : 'local'} install sets permissions before launch`, () => {
    const harness = prepareHarness('android')
    harness.env.ORO_ANDROID_CI = ci
    const { result, commands } = runHarness(harness)
    assert.equal(result.status, 0, result.stderr)
    const install = commands.find(({ command, args }) => command === 'adb' && args[0] === 'install')
    assert.equal(install.args.includes('-g'), Boolean(ci))
    assert.ok(install.args.at(-1).endsWith(`app-${ci ? 'dev' : 'live'}-debug.apk`))
    const build = commands.find(({ command }) => command === 'oroc')
    assert.ok(!build.args.includes('-r'))
    assert.equal(commands.at(-1).command, 'mock-shell')
    assert.equal(commands.at(-2).args[0], 'push', 'fixtures must be copied before the log poller launches the app')
  })
}
