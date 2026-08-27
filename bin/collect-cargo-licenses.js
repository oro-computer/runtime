#!/usr/bin/env node

import { execFileSync } from 'node:child_process'
import {
  copyFileSync,
  existsSync,
  mkdirSync,
  readdirSync,
  readFileSync,
  statSync,
  writeFileSync
} from 'node:fs'
import path from 'node:path'

const [crateArgument, destinationArgument] = process.argv.slice(2)
if (!crateArgument || !destinationArgument) {
  console.error(
    'usage: collect-cargo-licenses.js <crate-directory> <destination-directory>'
  )
  process.exit(1)
}

const crateDirectory = path.resolve(crateArgument)
const destinationDirectory = path.resolve(destinationArgument)
const manifestPath = path.join(crateDirectory, 'Cargo.toml')
if (!existsSync(manifestPath)) {
  console.error(`Cargo manifest not found: ${manifestPath}`)
  process.exit(1)
}

const rustcVersion = execFileSync('rustc', ['-vV'], { encoding: 'utf8' })
const targetTriple = rustcVersion.match(/^host:\s*(\S+)$/m)?.[1]
if (!targetTriple) {
  console.error('Unable to determine the Rust host target')
  process.exit(1)
}

const metadata = JSON.parse(
  execFileSync(
    'cargo',
    [
      'metadata',
      '--format-version',
      '1',
      '--locked',
      '--offline',
      '--filter-platform',
      targetTriple,
      '--manifest-path',
      manifestPath
    ],
    { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 }
  )
)

mkdirSync(destinationDirectory, { recursive: true })

const resolvedIds = new Set(
  Array.isArray(metadata.resolve?.nodes)
    ? metadata.resolve.nodes.map((node) => node.id)
    : []
)
const licenseName = /^(?:license|licence|copying|notice)(?:[._-].*)?$/i

function sanitize (value) {
  return String(value).replace(/[^0-9A-Za-z._-]+/g, '_')
}

function licenseFilesForPackage (pkg) {
  const manifestDirectory = path.dirname(pkg.manifest_path)
  const files = new Set()

  if (pkg.license_file) {
    const configured = path.resolve(manifestDirectory, pkg.license_file)
    if (existsSync(configured) && statSync(configured).isFile()) {
      files.add(configured)
    }
  }

  const addDirectoryLicenses = (directory) => {
    if (!existsSync(directory)) return
    for (const entry of readdirSync(directory, { withFileTypes: true })) {
      if (entry.isFile() && licenseName.test(entry.name)) {
        files.add(path.join(directory, entry.name))
      }
    }
  }

  addDirectoryLicenses(manifestDirectory)

  if (!String(pkg.source || '').startsWith('registry+')) {
    let ancestor = path.dirname(manifestDirectory)
    for (let depth = 0; depth < 4; depth += 1) {
      addDirectoryLicenses(ancestor)
      const parent = path.dirname(ancestor)
      if (parent === ancestor) break
      ancestor = parent
    }
  }

  return [...files].sort()
}

const inventory = []
const missing = []
const declaredOnly = []
for (const pkg of metadata.packages || []) {
  if (resolvedIds.size > 0 && !resolvedIds.has(pkg.id)) continue

  const licenseFiles = licenseFilesForPackage(pkg)
  const copiedFiles = []
  for (const source of licenseFiles) {
    const destinationName = [
      'cargo',
      sanitize(pkg.name),
      sanitize(pkg.version),
      sanitize(path.basename(source))
    ].join('-')
    copyFileSync(source, path.join(destinationDirectory, destinationName))
    copiedFiles.push(destinationName)
  }

  inventory.push({
    name: pkg.name,
    version: pkg.version,
    license: pkg.license || null,
    source: pkg.source || null,
    repository: pkg.repository || null,
    licenseFiles: copiedFiles
  })

  if (pkg.name !== 'oro-iroh' && copiedFiles.length === 0) {
    if (pkg.license) {
      declaredOnly.push(`${pkg.name}@${pkg.version} (${pkg.license})`)
    } else {
      missing.push(`${pkg.name}@${pkg.version}`)
    }
  }
}

inventory.sort((left, right) =>
  `${left.name}@${left.version}`.localeCompare(`${right.name}@${right.version}`)
)
writeFileSync(
  path.join(destinationDirectory, 'cargo-dependencies.json'),
  `${JSON.stringify(inventory, null, 2)}\n`
)

if (missing.length > 0) {
  console.error('Cargo dependencies without license metadata or license text:')
  for (const entry of missing) console.error(`- ${entry}`)
  process.exit(1)
}

if (declaredOnly.length > 0) {
  console.log(
    `Recorded SPDX license declarations for ${declaredOnly.length} packages without bundled license files`
  )
}

// Ensure the generated inventory can be read before reporting success.
JSON.parse(
  readFileSync(path.join(destinationDirectory, 'cargo-dependencies.json'))
)
console.log(`Collected Cargo license files for ${inventory.length} packages`)
