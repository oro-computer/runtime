import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(__dirname, '..', '..')

function readFile (relativePath) {
  return readFileSync(path.join(repoRoot, relativePath), 'utf8')
}

test('ci_version_check.sh prefers oroc CLI', () => {
  const script = readFile('bin/ci_version_check.sh')
  assert.match(
    script,
    /CLI_BIN="\${ORO_CLI_BIN:-oroc}"/,
    'ci_version_check.sh should default to oroc'
  )
  assert.doesNotMatch(
    script,
    /\bssc\b/,
    'ci_version_check.sh should not reference legacy CLI names'
  )
})

test('ci_version_check.ps1 prefers oroc CLI', () => {
  const script = readFile('bin/ci_version_check.ps1')
  assert.match(
    script,
    /\$cliBin = if \(\$env:ORO_CLI_BIN .* "oroc"/,
    'ci_version_check.ps1 should default to oroc'
  )
  assert.doesNotMatch(
    script,
    /\bssc\b/,
    'ci_version_check.ps1 should not reference legacy CLI names'
  )
})

test('uninstall.sh removes the oroc binary', () => {
  const script = readFile('bin/uninstall.sh')
  assert.match(
    script,
    /declare bins=\("\$PREFIX\/bin\/oroc"\)/,
    'uninstall.sh should clean up oroc'
  )
  assert.doesNotMatch(
    script,
    /\bssc\b/,
    'uninstall.sh should not reference legacy CLI names'
  )
})

test('functions.sh sudo prompt references Oro CLI', () => {
  const script = readFile('bin/functions.sh')
  assert.match(
    script,
    /'oroc' would like to use 'sudo'/,
    'sudo prompt should mention oroc as the CLI'
  )
  assert.doesNotMatch(
    script,
    /\bssc\b/,
    'functions.sh should not reference legacy CLI names'
  )
})

test('publish-npm-modules.sh stages Oro CLI packages', () => {
  const script = readFile('bin/publish-npm-modules.sh')
  assert.match(
    script,
    /CLI_PACKAGE_SPECS=\([\s\S]*"@orocomputer:runtime"[\s\S]*\)/,
    'publish-npm-modules.sh should stage @orocomputer/runtime'
  )
  assert.doesNotMatch(
    script,
    /@socketsupply|socketsupply|socket-node|socket\b/,
    'publish-npm-modules.sh should not reference legacy packages'
  )
})

test('version.sh bumps the Oro CLI package', () => {
  const script = readFile('bin/version.sh')
  assert.match(
    script,
    /npm\/packages\/@orocomputer\/runtime-node/,
    'version.sh must bump the Oro CLI package'
  )
  assert.doesNotMatch(
    script,
    /@socketsupply|socketsupply|socket-node/,
    'version.sh should not reference legacy packages'
  )
})

test('runtime-artifacts.sh defines Oro artifact name', () => {
  const script = readFile('bin/runtime-artifacts.sh')
  assert.match(
    script,
    /ORO_RUNTIME_ARTIFACT_NAME:=oro-runtime/,
    'runtime artifacts should default to oro-runtime naming'
  )
  assert.doesNotMatch(
    script,
    /socket-runtime/,
    'runtime artifacts should not define legacy aliases'
  )
})
