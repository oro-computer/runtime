#!/usr/bin/env node

import {
  cpSync,
  existsSync,
  mkdtempSync,
  readFileSync,
  readdirSync,
  rmSync,
  statSync,
  writeFileSync
} from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, relative, resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const targetDirectory = join(root, 'api', 'latica')
const targetWrapper = join(root, 'api', 'latica.js')
const wrapperSource = [
  "import def from './latica/index.js'",
  "export * from './latica/index.js'",
  'export default def',
  ''
].join('\n')

function collectFiles (directory) {
  const files = []

  for (const entry of readdirSync(directory)) {
    const path = join(directory, entry)
    if (statSync(path).isDirectory()) {
      files.push(...collectFiles(path))
    } else {
      files.push(path)
    }
  }

  return files
}

function validateGeneratedProtocol (directory, wrapper) {
  const index = join(directory, 'index.js')
  if (!existsSync(index)) {
    throw new Error(`generated protocol is missing ${index}`)
  }

  const unresolvedImports = []
  for (const file of collectFiles(directory)) {
    if (!file.endsWith('.js')) continue
    if (/(['"])oro:[^'"]+\1/.test(readFileSync(file, 'utf8'))) {
      unresolvedImports.push(relative(root, file))
    }
  }

  if (unresolvedImports.length > 0) {
    throw new Error(
      `generated protocol contains unresolved oro: imports: ${unresolvedImports.join(', ')}`
    )
  }

  if (!existsSync(wrapper) || readFileSync(wrapper, 'utf8') !== wrapperSource) {
    throw new Error('api/latica.js does not match the generated wrapper')
  }
}

function rewriteRuntimeImports (directory) {
  for (const file of collectFiles(directory)) {
    if (!file.endsWith('.js')) continue

    const relativeFile = relative(directory, file)
    const importerDirectory = dirname(join('latica', relativeFile))
    const source = readFileSync(file, 'utf8').replace(
      /(['"])oro:([^'"]+)\1/g,
      (_, quote, moduleName) => {
        let specifier = relative(importerDirectory, `${moduleName}.js`)
          .split(sep)
          .join('/')
        if (!specifier.startsWith('.')) specifier = `./${specifier}`
        return `${quote}${specifier}${quote}`
      }
    )
    writeFileSync(file, source)
  }
}

function usage () {
  console.error(`usage:
  npm run update-network-protocol -- /absolute/path/to/latica
  LATICA_SOURCE_DIR=/absolute/path/to/latica npm run update-network-protocol
  npm run update-network-protocol -- --check

The source path may point to a checkout root containing src/index.js or directly
to its src directory. The updater stages and validates all generated files before
replacing api/latica and api/latica.js.`)
}

const requestedSource = process.argv[2] || process.env.LATICA_SOURCE_DIR || ''
if (requestedSource === '--check') {
  validateGeneratedProtocol(targetDirectory, targetWrapper)
  console.log('ok - vendored network protocol is self-contained')
  process.exit(0)
}

if (!requestedSource) {
  usage()
  process.exit(2)
}

const sourceRoot = resolve(requestedSource)
const sourceDirectory = existsSync(join(sourceRoot, 'src', 'index.js'))
  ? join(sourceRoot, 'src')
  : sourceRoot

if (!existsSync(join(sourceDirectory, 'index.js'))) {
  console.error(`Network protocol source does not contain index.js: ${sourceDirectory}`)
  usage()
  process.exit(2)
}

const stagingRoot = mkdtempSync(join(tmpdir(), 'oro-latica-update-'))
const stagedDirectory = join(stagingRoot, 'latica')
const stagedWrapper = join(stagingRoot, 'latica.js')
const previousDirectory = join(stagingRoot, 'previous-latica')
const previousWrapper = join(stagingRoot, 'previous-latica.js')
const hadPreviousDirectory = existsSync(targetDirectory)
const hadPreviousWrapper = existsSync(targetWrapper)

try {
  cpSync(sourceDirectory, stagedDirectory, { recursive: true })
  rewriteRuntimeImports(stagedDirectory)
  writeFileSync(stagedWrapper, wrapperSource)
  validateGeneratedProtocol(stagedDirectory, stagedWrapper)

  if (hadPreviousDirectory) {
    cpSync(targetDirectory, previousDirectory, { recursive: true })
  }
  if (hadPreviousWrapper) cpSync(targetWrapper, previousWrapper)

  try {
    rmSync(targetDirectory, { recursive: true, force: true })
    cpSync(stagedDirectory, targetDirectory, { recursive: true })
    cpSync(stagedWrapper, targetWrapper)
    validateGeneratedProtocol(targetDirectory, targetWrapper)
  } catch (error) {
    rmSync(targetDirectory, { recursive: true, force: true })
    if (hadPreviousDirectory) {
      cpSync(previousDirectory, targetDirectory, { recursive: true })
    }
    if (hadPreviousWrapper) {
      cpSync(previousWrapper, targetWrapper)
    } else {
      rmSync(targetWrapper, { force: true })
    }
    throw error
  }

  console.log(`ok - updated vendored network protocol from ${sourceDirectory}`)
} finally {
  rmSync(stagingRoot, { recursive: true, force: true })
}
