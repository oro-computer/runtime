import fs from 'node:fs'
import path from 'node:path'
import process from 'node:process'
import { fileURLToPath } from 'node:url'

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const sourceRoots = ['include', 'src'].map((directory) => path.join(repoRoot, directory))
const sourceExtensions = new Set(['.c', '.cc', '.cpp', '.h', '.hh', '.hpp', '.m', '.mm'])
const failures = []

function visit (directory) {
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const filename = path.join(directory, entry.name)
    if (entry.isDirectory()) {
      visit(filename)
    } else if (entry.isFile() && sourceExtensions.has(path.extname(entry.name))) {
      checkFile(filename)
    }
  }
}

function checkFile (filename) {
  const source = fs.readFileSync(filename, 'utf8')
  const includePattern = /^\s*#\s*(?:include|import)\s+"([^"]+)"/gm
  let match

  while ((match = includePattern.exec(source)) !== null) {
    const specifier = match[1]
    if (!specifier.startsWith('.')) {
      continue
    }

    const target = path.resolve(path.dirname(filename), specifier)
    if (!fs.existsSync(target)) {
      const line = source.slice(0, match.index).split('\n').length
      failures.push(`${path.relative(repoRoot, filename)}:${line}: ${specifier}`)
    }
  }
}

for (const sourceRoot of sourceRoots) {
  visit(sourceRoot)
}

if (failures.length > 0) {
  console.error('Invalid local C/C++ includes:')
  for (const failure of failures) {
    console.error(`  ${failure}`)
  }
  process.exitCode = 1
} else {
  console.log('Local C/C++ includes resolve correctly')
}
