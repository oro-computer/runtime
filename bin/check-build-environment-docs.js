import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs'
import path from 'node:path'
import process from 'node:process'
import { fileURLToPath } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const ignoredDirectories = new Set([
  '.agents',
  '.cache',
  '.git',
  '.npm-cache',
  '.oro_home',
  'audit',
  'build',
  'node_modules',
  'tmp'
])
const documentationExtensions = new Set(['.md', '.txt'])
const publicHelpFiles = [
  'bin/install.ps1',
  'bin/install.sh',
  'bin/publish-npm-modules.sh',
  'src/cli/templates.hh'
]
const failures = []

function isDocumentation (filename) {
  if (documentationExtensions.has(path.extname(filename))) {
    return true
  }

  const relative = path.relative(root, filename)
  return relative.startsWith(`share${path.sep}man${path.sep}`)
}

function inspectPairedControls (filename) {
  const content = readFileSync(filename, 'utf8')
  const hasAndroid = content.includes('NO_ANDROID')
  const hasIos = content.includes('NO_IOS')

  if (hasAndroid !== hasIos) {
    failures.push(
      `${path.relative(root, filename)} mentions ${hasAndroid ? 'NO_ANDROID' : 'NO_IOS'} without the other independent control`
    )
  }
}

function walk (directory) {
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    if (entry.isDirectory()) {
      if (!ignoredDirectories.has(entry.name)) {
        walk(path.join(directory, entry.name))
      }
      continue
    }

    const filename = path.join(directory, entry.name)
    if (entry.isFile() && isDocumentation(filename)) {
      inspectPairedControls(filename)
    }
  }
}

walk(root)

for (const relative of publicHelpFiles) {
  const filename = path.join(root, relative)
  if (!existsSync(filename) || !statSync(filename).isFile()) {
    failures.push(`${relative} is missing`)
    continue
  }
  inspectPairedControls(filename)
}

const canonical = readFileSync(
  path.join(root, 'docs', 'BUILD_ENVIRONMENT.md'),
  'utf8'
)
const requiredCanonicalStatements = [
  ['independent controls', 'independent, presence-based environment'],
  ['false-like value behavior', 'NO_ANDROID=0'],
  ['false-like iOS value behavior', 'NO_IOS=false'],
  ['desktop-only invocation', 'NO_ANDROID=1 NO_IOS=1 ./bin/install.sh'],
  ['first-time setup distinction', '`--no-android-fte` is not `NO_ANDROID`'],
  ['application-target distinction', 'do not select the target for a downstream application build']
]

for (const [description, statement] of requiredCanonicalStatements) {
  if (!canonical.includes(statement)) {
    failures.push(
      `docs/BUILD_ENVIRONMENT.md is missing the ${description}: ${statement}`
    )
  }
}

if (failures.length > 0) {
  for (const failure of failures) {
    console.error(`not ok - ${failure}`)
  }
  process.exitCode = 1
} else {
  console.log('ok - source-build environment documentation is consistent')
}
