#!/usr/bin/env node

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)))
const version = String(process.argv[2] || '').replace(/^v/, '')
const semverPattern = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-(?:0|[1-9]\d*|[A-Za-z-][0-9A-Za-z-]*)(?:\.(?:0|[1-9]\d*|[A-Za-z-][0-9A-Za-z-]*))*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$/

if (!semverPattern.test(version)) {
  console.error(`Invalid semantic version: ${version || '<empty>'}`)
  process.exit(1)
}

const packageDirectories = [
  'runtime',
  'runtime-darwin-arm64',
  'runtime-darwin-x64',
  'runtime-linux-arm64',
  'runtime-linux-x64',
  'runtime-node',
  'runtime-win32-x64'
]

function writeJson (filename, update) {
  const document = JSON.parse(fs.readFileSync(filename, 'utf8'))
  update(document)
  fs.writeFileSync(filename, `${JSON.stringify(document, null, 2)}\n`)
}

fs.writeFileSync(path.join(root, 'VERSION.txt'), `${version}\n`)
writeJson(path.join(root, 'package.json'), (document) => {
  document.version = version
})
writeJson(path.join(root, 'clib.json'), (document) => {
  document.version = version
})

for (const directory of packageDirectories) {
  const manifest = path.join(
    root,
    'npm/packages/@oro-computer',
    directory,
    'package.json'
  )
  writeJson(manifest, (document) => {
    document.version = version
    if (document.optionalDependencies) {
      for (const name of Object.keys(document.optionalDependencies)) {
        if (name.startsWith('@oro-computer/runtime-')) {
          document.optionalDependencies[name] = version
        }
      }
    }
  })
}

const cargoManifest = path.join(root, 'rust/oro-iroh/Cargo.toml')
const cargoSource = fs.readFileSync(cargoManifest, 'utf8')
const cargoVersionPattern = /(^\[package\][\s\S]*?^version\s*=\s*)"[^"]+"/m
if (!cargoVersionPattern.test(cargoSource)) {
  throw new Error('Unable to find the package version in rust/oro-iroh/Cargo.toml')
}
const cargoUpdated = cargoSource.replace(
  cargoVersionPattern,
  `$1"${version}"`
)

fs.writeFileSync(cargoManifest, cargoUpdated)

const cargoLock = path.join(root, 'rust/oro-iroh/Cargo.lock')
const cargoLockSource = fs.readFileSync(cargoLock, 'utf8')
const cargoLockVersionPattern = /(\[\[package\]\]\r?\nname = "oro-iroh"\r?\nversion = ")[^"]+"/
if (!cargoLockVersionPattern.test(cargoLockSource)) {
  throw new Error('Unable to find the oro-iroh package in Cargo.lock')
}
const cargoLockUpdated = cargoLockSource.replace(
  cargoLockVersionPattern,
  `$1${version}"`
)

fs.writeFileSync(cargoLock, cargoLockUpdated)
