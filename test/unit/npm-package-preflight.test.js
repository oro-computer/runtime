import test from 'node:test'
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { spawnSync } from 'node:child_process'
import { closeSync, existsSync, ftruncateSync, mkdirSync, mkdtempSync, openSync, readdirSync, readFileSync, symlinkSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { NPM_PACKAGE_SIZE_LIMIT, verifyNpmPackageSize, verifyNpmPackSizes } from '../../bin/verify-npm-package-size.js'

const root = fileURLToPath(new URL('../../', import.meta.url))
const helper = path.join(root, 'bin/verify-npm-package-size.js')
const workflow = readFileSync(path.join(root, '.github/workflows/publish-npm.yml'), 'utf8').replace(/\r\n/g, '\n')
const version = '0.2.0'
const packageNames = ['runtime-linux-x64', 'runtime-linux-arm64', 'runtime-darwin-x64', 'runtime-darwin-arm64', 'runtime-win32-x64', 'runtime-node', 'runtime']
const hasBash = spawnSync('bash', ['--version'], { stdio: 'ignore' }).status === 0
const shellOptions = { skip: hasBash ? false : 'The Ubuntu publication workflow requires Bash' }

function createFile (filename, size) {
  const descriptor = openSync(filename, 'w')
  try {
    ftruncateSync(descriptor, size)
  } finally {
    closeSync(descriptor)
  }
}

function workflowScript (name) {
  const step = workflow.split(`      - name: ${name}\n`)[1]
  assert.ok(step, `Missing workflow step: ${name}`)
  const lines = step.split('        run: |\n')[1].split('\n')
  const script = []
  for (const line of lines) {
    if (line && !line.startsWith('          ')) break
    script.push(line.slice(10))
  }
  return script.join('\n')
}

function shellPath (filename) {
  return filename.split(path.sep).join('/')
}

test('npm archive size checks accept the exact budget and reject one byte over', () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro npm size '))
  const archive = path.join(directory, 'package with spaces.tgz')
  assert.equal(NPM_PACKAGE_SIZE_LIMIT, 190_000_000)
  createFile(archive, NPM_PACKAGE_SIZE_LIMIT)
  assert.equal(verifyNpmPackageSize(archive), NPM_PACKAGE_SIZE_LIMIT)
  createFile(archive, NPM_PACKAGE_SIZE_LIMIT + 1)
  assert.throws(() => verifyNpmPackageSize(archive), /190000001 compressed bytes.*190000000 bytes.*base64/)
})

test('npm archive size checks reject missing, empty, and non-file inputs', () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro npm invalid size '))
  const archive = path.join(directory, 'empty.tgz')
  assert.throws(() => verifyNpmPackageSize(archive), { code: 'ENOENT' })
  createFile(archive, 0)
  assert.throws(() => verifyNpmPackageSize(archive), /positive integer compressed size/)
  assert.throws(() => verifyNpmPackageSize(directory), /not a regular file/)
})

test('npm pack reports are checked by compressed size for every package', () => {
  const entry = { filename: 'package.tgz', size: NPM_PACKAGE_SIZE_LIMIT, unpackedSize: NPM_PACKAGE_SIZE_LIMIT * 4 }
  assert.deepEqual(verifyNpmPackSizes(JSON.stringify([entry])), [{ filename: entry.filename, size: entry.size }])
  assert.throws(() => verifyNpmPackSizes(JSON.stringify([entry, { filename: 'oversized.tgz', size: NPM_PACKAGE_SIZE_LIMIT + 1 }])), /oversized.tgz.*exceeding/)
  for (const report of [null, {}, [], [null], [{ size: 1 }], [{ filename: '' }], [{ filename: 'x', size: '1' }], [{ filename: 'x', size: 0 }], [{ filename: 'x', size: -1 }], [{ filename: 'x', size: 0.5 }]]) {
    assert.throws(() => verifyNpmPackSizes(JSON.stringify(report)), Error, JSON.stringify(report))
  }
  assert.throws(() => verifyNpmPackSizes('invalid JSON'), SyntaxError)
})

test('size CLI checks npm pack stdin and actual tarballs without reading their contents', () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro npm size cli '))
  const archive = path.join(directory, 'package.tgz')
  createFile(archive, NPM_PACKAGE_SIZE_LIMIT)
  for (const [args, input, status] of [
    [[archive], undefined, 0],
    [['--pack-json'], JSON.stringify([{ filename: 'package.tgz', size: NPM_PACKAGE_SIZE_LIMIT }]), 0],
    [['--pack-json'], JSON.stringify([{ filename: 'package.tgz', size: NPM_PACKAGE_SIZE_LIMIT + 1 }]), 1],
    [['--pack-json'], '[]', 1],
    [[], undefined, 1]
  ]) {
    const result = spawnSync(process.execPath, [helper, ...args], { input, encoding: 'utf8', timeout: 10000 })
    assert.ifError(result.error)
    assert.equal(result.status, status, result.stdout + result.stderr)
  }
})

test('size validation still runs when the CLI directory is a symlink or junction', () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro npm size alias '))
  const alias = path.join(directory, 'linked bin')
  symlinkSync(path.dirname(helper), alias, 'junction')
  const result = spawnSync(process.execPath, [path.join(alias, path.basename(helper)), '--pack-json'], {
    input: JSON.stringify([{ filename: 'oversized.tgz', size: NPM_PACKAGE_SIZE_LIMIT + 1 }]),
    encoding: 'utf8',
    timeout: 10000
  })
  assert.ifError(result.error)
  assert.equal(result.status, 1, result.stdout + result.stderr)
  assert.match(result.stderr, /oversized.tgz.*exceeding/)
})

test('real npm prepack output cannot corrupt size validation and failed hooks stop packing', shellOptions, () => {
  const staging = readFileSync(path.join(root, 'bin/publish-npm-modules.sh'), 'utf8').replace(/\r\n/g, '\n')
  const pack = staging.slice(staging.indexOf('function _pack_or_link ()'), staging.indexOf('\nfunction should_stage_target_directory'))
  assert.ok(pack.includes('npm pack'), 'Execute the real staging pack function')
  for (const exitCode of [0, 23]) {
    const directory = mkdtempSync(path.join(tmpdir(), 'oro npm lifecycle pack '))
    const destination = path.join(directory, 'tarballs')
    const marker = path.join(directory, 'prepack-ran')
    mkdirSync(destination)
    writeFileSync(path.join(directory, 'package.json'), JSON.stringify({
      name: 'oro-size-prepack-fixture',
      version: '0.0.0',
      files: ['payload.txt'],
      scripts: { prepack: 'node prepack.cjs' }
    }))
    writeFileSync(path.join(directory, 'payload.txt'), 'Public packed fixture')
    writeFileSync(path.join(directory, 'prepack.cjs'), `
      require('node:fs').writeFileSync(process.env.ORO_NPM_HOOK_MARKER, 'executed')
      console.log('prepack stdout is not JSON')
      process.exit(${exitCode})
    `)
    const result = spawnSync('bash', ['-e', '-o', 'pipefail', '-c', `
      node () { "$NODE_COMMAND" "$@"; }
      root="$SOURCE_ROOT"
      do_global_link=0
      args=(--pack-destination "$PACK_DIRECTORY" --foreground-scripts=true)
      ${pack}
      _pack_or_link
    `], {
      cwd: directory,
      encoding: 'utf8',
      timeout: 30000,
      env: {
        ...process.env,
        NODE_COMMAND: shellPath(process.execPath),
        SOURCE_ROOT: shellPath(root),
        PACK_DIRECTORY: shellPath(destination),
        ORO_NPM_HOOK_MARKER: marker,
        npm_config_cache: path.join(directory, 'npm-cache')
      }
    })
    assert.ifError(result.error)
    assert.equal(result.status, exitCode, result.stdout + result.stderr)
    assert.equal(readFileSync(marker, 'utf8'), 'executed', 'prepack must run')
    const archives = readdirSync(destination)
    if (exitCode === 0) {
      assert.deepEqual(archives, ['oro-size-prepack-fixture-0.0.0.tgz'])
      assert.ok(verifyNpmPackageSize(path.join(destination, archives[0])) > 0)
      assert.match(result.stdout, /Verified npm package size: oro-size-prepack-fixture-0.0.0.tgz/)
      assert.doesNotMatch(result.stdout, /prepack stdout is not JSON/)
    } else {
      assert.deepEqual(archives, [], 'a failed prepack must not produce a package')
      assert.doesNotMatch(result.stdout, /Verified npm package size:/)
    }
  }
})

function publicationFixture (responses = {}, invalidArchive) {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro npm preflight '))
  const packDirectory = path.join(directory, 'npm-packs')
  mkdirSync(packDirectory)
  const calls = path.join(directory, 'npm-calls.jsonl')
  const registry = {}
  const archives = packageNames.map((name, index) => {
    const filename = path.join(packDirectory, `oro-computer-${name}-${version}.tgz`)
    if (invalidArchive?.index === index) {
      if (invalidArchive.size !== undefined) createFile(filename, invalidArchive.size)
    } else {
      writeFileSync(filename, `Verified fixture package: ${name}`)
    }
    const response = responses[index]
    registry[`@oro-computer/${name}@${version}`] = response === 'matching'
      ? { status: 0, output: JSON.stringify(`sha512-${createHash('sha512').update(readFileSync(filename)).digest('base64')}`) }
      : response || { status: 1, output: JSON.stringify({ error: { code: 'E404' } }) }
    return filename
  })
  const mock = path.join(directory, 'npm-mock.cjs')
  writeFileSync(mock, `
    const assert = require('node:assert/strict')
    const fs = require('node:fs')
    const crypto = require('node:crypto')
    const args = process.argv.slice(2)
    if (args[0] === 'openssl') {
      assert.deepEqual(args.slice(0, 4), ['openssl', 'dgst', '-sha512', '-binary'])
      assert.equal(args.length, 5)
      process.stdout.write(crypto.createHash('sha512').update(fs.readFileSync(args[4])).digest())
    } else if (args[0] === 'base64') {
      assert.deepEqual(args, ['base64', '-w', '0'])
      process.stdout.write(fs.readFileSync(0).toString('base64'))
    } else {
      fs.appendFileSync(process.env.MOCK_NPM_CALLS, JSON.stringify(args) + '\\n')
      if (args[0] === 'view') {
        assert.deepEqual(args.slice(2), ['dist.integrity', '--json'])
        const response = JSON.parse(process.env.MOCK_NPM_REGISTRY)[args[1]]
        assert.ok(response, 'Unexpected registry lookup: ' + args[1])
        process.stdout.write(response.output)
        process.exitCode = response.status
      } else {
        assert.equal(args[0], 'publish')
        assert.deepEqual(args.slice(2), ['--access', 'public'])
      }
    }
  `)
  const result = spawnSync('bash', ['-e', '-o', 'pipefail', '-c', `
    node () { "$NODE_COMMAND" "$@"; }
    npm () { "$NODE_COMMAND" "$MOCK_NPM" "$@"; }
    openssl () { "$NODE_COMMAND" "$MOCK_NPM" openssl "$@"; }
    base64 () { "$NODE_COMMAND" "$MOCK_NPM" base64 "$@"; }
    ${workflowScript('Publish packages in dependency order')}
  `], {
    cwd: root,
    encoding: 'utf8',
    timeout: 60000,
    env: {
      ...process.env,
      RUNNER_TEMP: shellPath(directory),
      RELEASE_VERSION: version,
      NODE_COMMAND: shellPath(process.execPath),
      MOCK_NPM: shellPath(mock),
      MOCK_NPM_CALLS: calls,
      MOCK_NPM_REGISTRY: JSON.stringify(registry)
    }
  })
  assert.ifError(result.error)
  const operations = existsSync(calls) ? readFileSync(calls, 'utf8').trim().split('\n').map(line => JSON.parse(line)) : []
  return { result, operations, archives }
}

test('publication checks all seven registry versions before publishing in dependency order', shellOptions, () => {
  const { result, operations, archives } = publicationFixture()
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.deepEqual(operations.map(operation => operation[0]), [...Array(7).fill('view'), ...Array(7).fill('publish')])
  assert.deepEqual(operations.slice(7).map(operation => path.resolve(operation[1])), archives)
})

test('publication retries skip exactly matching already published packages', shellOptions, () => {
  const { result, operations, archives } = publicationFixture({ 0: 'matching', 1: 'matching', 2: 'matching' })
  assert.equal(result.status, 0, result.stdout + result.stderr)
  assert.deepEqual(operations.map(operation => operation[0]), [...Array(7).fill('view'), ...Array(4).fill('publish')])
  assert.deepEqual(operations.slice(7).map(operation => path.resolve(operation[1])), archives.slice(3))
})

test('a later immutable-version conflict stops the release before any publication', shellOptions, () => {
  const { result, operations } = publicationFixture({ 3: { status: 0, output: JSON.stringify('sha512-different') } })
  assert.equal(result.status, 1, result.stdout + result.stderr)
  assert.match(result.stderr, /already exists with different package contents/)
  assert.deepEqual(operations.map(operation => operation[0]), Array(4).fill('view'))
})

test('oversized, empty, or missing later archives stop publication before any registry access', shellOptions, () => {
  for (const size of [NPM_PACKAGE_SIZE_LIMIT + 1, 0, undefined]) {
    const { result, operations } = publicationFixture({}, { index: 3, size })
    assert.equal(result.status, 1, result.stdout + result.stderr)
    assert.deepEqual(operations, [])
  }
})

test('registry errors and malformed successful responses never authorize publication', shellOptions, () => {
  const responses = [
    ...['E403', 'E401', 'ETIMEDOUT', 'E500'].map(code => ({ status: 1, output: JSON.stringify({ error: { code } }) })),
    { status: 1, output: 'E404 is mentioned in an invalid response' },
    { status: 1, output: JSON.stringify({ code: 'E404' }) },
    { status: 0, output: '' },
    { status: 0, output: 'null' },
    { status: 0, output: JSON.stringify({ error: { code: 'E404' } }) }
  ]
  for (const response of responses) {
    const { result, operations } = publicationFixture({ 3: response })
    assert.equal(result.status, 1, result.stdout + result.stderr)
    assert.deepEqual(operations.map(operation => operation[0]), Array(4).fill('view'))
  }
})

test('complete family verification rejects an oversized later archive before manifest extraction', shellOptions, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro npm family size '))
  const packDirectory = path.join(directory, 'npm-packs')
  mkdirSync(packDirectory)
  for (const [index, name] of packageNames.entries()) {
    createFile(path.join(packDirectory, `oro-computer-${name}-${version}.tgz`), index === 3 ? NPM_PACKAGE_SIZE_LIMIT + 1 : 1)
  }
  const result = spawnSync('bash', ['-e', '-o', 'pipefail', '-c', `
    node () { "$NODE_COMMAND" "$@"; }
    tar () { echo unexpected-manifest-extraction >&2; return 19; }
    ${workflowScript('Verify complete npm package set')}
  `], {
    cwd: root,
    encoding: 'utf8',
    timeout: 10000,
    env: { ...process.env, RUNNER_TEMP: shellPath(directory), RELEASE_VERSION: version, NODE_COMMAND: shellPath(process.execPath) }
  })
  assert.ifError(result.error)
  assert.equal(result.status, 1, result.stdout + result.stderr)
  assert.match(result.stderr, /runtime-darwin-arm64.*exceeding.*190000000 bytes/)
  assert.doesNotMatch(result.stderr, /unexpected-manifest-extraction/)
})
