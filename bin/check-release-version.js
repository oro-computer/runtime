#!/usr/bin/env node

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)))
const requestedVersion =
  process.argv[2] ||
  process.env.RELEASE_VERSION ||
  fs.readFileSync(path.join(root, 'VERSION.txt'), 'utf8')
const expected = String(requestedVersion).trim().replace(/^v/, '')
const semverPattern = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-(?:0|[1-9]\d*|[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9]\d*|[A-Za-z-][0-9A-Za-z-]*))*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$/
const errors = []

if (!semverPattern.test(expected)) {
  console.error(`Invalid semantic version: ${expected || '<empty>'}`)
  process.exit(1)
}

function check (label, observed) {
  if (observed !== expected) {
    errors.push(`${label}: expected ${expected}, found ${observed}`)
  }
}

check(
  'VERSION.txt',
  fs.readFileSync(path.join(root, 'VERSION.txt'), 'utf8').trim()
)
const rootPackage = JSON.parse(
  fs.readFileSync(path.join(root, 'package.json'), 'utf8')
)
check('package.json', rootPackage.version)
if (rootPackage.private !== true) {
  errors.push('package.json: root workspace must remain private')
}
const clib = JSON.parse(
  fs.readFileSync(path.join(root, 'clib.json'), 'utf8')
)
check('clib.json', clib.version)
if (clib.license !== 'Apache-2.0') {
  errors.push(`clib.json: expected Apache-2.0 license, found ${clib.license}`)
}

const packageRoot = path.join(root, 'npm/packages/@oro-computer')
const expectedPackageNames = new Map([
  ['runtime', '@oro-computer/runtime'],
  ['runtime-darwin-arm64', '@oro-computer/runtime-darwin-arm64'],
  ['runtime-darwin-x64', '@oro-computer/runtime-darwin-x64'],
  ['runtime-linux-arm64', '@oro-computer/runtime-linux-arm64'],
  ['runtime-linux-x64', '@oro-computer/runtime-linux-x64'],
  ['runtime-node', '@oro-computer/runtime-node'],
  ['runtime-win32-x64', '@oro-computer/runtime-win32-x64']
])
const packageEntries = fs
  .readdirSync(packageRoot, { withFileTypes: true })
  .filter((entry) => entry.isDirectory() && entry.name.startsWith('runtime'))

for (const directory of expectedPackageNames.keys()) {
  if (!packageEntries.some((entry) => entry.name === directory)) {
    errors.push(`npm package family: missing directory ${directory}`)
  }
}

for (const entry of packageEntries) {
  const expectedName = expectedPackageNames.get(entry.name)
  if (!expectedName) {
    errors.push(`npm package family: unexpected directory ${entry.name}`)
    continue
  }

  const manifestPath = path.join(packageRoot, entry.name, 'package.json')
  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'))
  if (manifest.name !== expectedName) {
    errors.push(
      `${entry.name}: expected package name ${expectedName}, found ${manifest.name}`
    )
  }
  check(manifest.name, manifest.version)
  if (manifest.license !== 'Apache-2.0') {
    errors.push(
      `${manifest.name}: expected Apache-2.0 license, found ${manifest.license}`
    )
  }
  for (const [name, version] of Object.entries(
    manifest.optionalDependencies || {}
  )) {
    if (name.startsWith('@oro-computer/runtime-')) {
      check(`${manifest.name} -> ${name}`, version)
    }
  }
}

const metaManifest = JSON.parse(
  fs.readFileSync(path.join(packageRoot, 'runtime/package.json'), 'utf8')
)
const expectedOptionalDependencies = [...expectedPackageNames.values()]
  .filter((name) => name !== '@oro-computer/runtime')
  .filter((name) => name !== '@oro-computer/runtime-node')
  .sort()
const observedOptionalDependencies = Object.keys(
  metaManifest.optionalDependencies || {}
).sort()
if (
  observedOptionalDependencies.join('\n') !==
  expectedOptionalDependencies.join('\n')
) {
  errors.push(
    `@oro-computer/runtime: expected optional dependencies ${expectedOptionalDependencies.join(', ')}, found ${observedOptionalDependencies.join(', ')}`
  )
}

const cargo = fs.readFileSync(
  path.join(root, 'rust/oro-iroh/Cargo.toml'),
  'utf8'
)
const cargoVersion = cargo.match(/^version\s*=\s*"([^"]+)"/m)?.[1]
check('rust/oro-iroh', cargoVersion)
const cargoLicense = cargo.match(/^license\s*=\s*"([^"]+)"/m)?.[1]
if (cargoLicense !== 'Apache-2.0') {
  errors.push(
    `rust/oro-iroh: expected Apache-2.0 license, found ${cargoLicense}`
  )
}

const cargoLock = fs.readFileSync(
  path.join(root, 'rust/oro-iroh/Cargo.lock'),
  'utf8'
)
const cargoLockVersion = cargoLock.match(
  /\[\[package\]\]\r?\nname = "oro-iroh"\r?\nversion = "([^"]+)"/
)?.[1]
check('rust/oro-iroh/Cargo.lock', cargoLockVersion)

const changelog = fs.readFileSync(path.join(root, 'CHANGELOG.md'), 'utf8')
const escapedVersion = expected.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
if (!new RegExp(`^## \\[${escapedVersion}\\]`, 'm').test(changelog)) {
  errors.push(`CHANGELOG.md: missing heading "## [${expected}]"`)
}

const nodePackageRoot = path.join(
  root,
  'npm/packages/@oro-computer/runtime-node'
)
for (const [sourceName, packageName] of [
  ['LICENSE.txt', 'LICENSE'],
  ['NOTICE', 'NOTICE'],
  ['THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md']
]) {
  const source = fs.readFileSync(path.join(root, sourceName), 'utf8')
  const packaged = fs.readFileSync(path.join(nodePackageRoot, packageName), 'utf8')
  if (source !== packaged) {
    errors.push(
      `@oro-computer/runtime-node/${packageName}: does not match ${sourceName}`
    )
  }
}

if (errors.length > 0) {
  for (const error of errors) console.error(`not ok - ${error}`)
  process.exit(1)
}

console.log(`ok - release metadata is synchronized at ${expected}`)
