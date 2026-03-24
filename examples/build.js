import fs from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import * as acorn from 'acorn'
import { simple as walk } from 'acorn-walk'

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)

const EXAMPLES_ROOT = __dirname
const OUTPUT_ROOT = path.join(EXAMPLES_ROOT, 'build')

const IGNORE_DIRS = new Set(['build', 'dist', 'scripts', 'ui'])

const EXTRA_ASSETS = new Map([
  [
    'kitchen-sink',
    [
      {
        source: 'window/secondary.html',
        target: 'examples/window/secondary.html'
      }
    ]
  ]
])

const EXTERNAL_NODE_MODULES = new Map([
  [
    'orosh',
    [
      '@xterm/xterm',
      '@xterm/addon-fit',
      '@xterm/addon-canvas',
      '@xterm/addon-search',
      '@xterm/addon-attach'
    ]
  ]
])

async function pathExists (filePath) {
  try {
    await fs.access(filePath)
    return true
  } catch {
    return false
  }
}

async function ensureEmptyDir (dirPath) {
  await fs.rm(dirPath, { recursive: true, force: true })
  await fs.mkdir(dirPath, { recursive: true })
}

function toPosix (value) {
  return value.split(path.sep).join('/')
}

async function copyStaticAssets (
  sourceDir,
  targetDir,
  { skip = new Set() } = {}
) {
  const entries = await fs.readdir(sourceDir, { withFileTypes: true })
  for (const entry of entries) {
    const from = path.join(sourceDir, entry.name)
    const to = path.join(targetDir, entry.name)
    const relPosix = toPosix(path.relative(sourceDir, from))
    if (skip.has(relPosix)) continue
    if (entry.isDirectory()) {
      // Skip directories that we will rebuild via bundling (JS modules)
      await fs.mkdir(to, { recursive: true })
      await copyStaticAssets(from, to, { skip })
    } else {
      if (entry.name === 'index.html') {
        continue
      }
      await fs.mkdir(path.dirname(to), { recursive: true })
      await fs.copyFile(from, to)
    }
  }
}

function collectModuleSpecifiers (source, filePath) {
  const ast = acorn.parse(source, {
    sourceType: 'module',
    ecmaVersion: 'latest',
    locations: false
  })

  const imports = []

  function record (node) {
    if (!node) return
    const value = node.value
    if (typeof value !== 'string') return
    if (!value.startsWith('.')) return
    imports.push({ node, specifier: value })
  }

  walk(ast, {
    ImportDeclaration (node) {
      record(node.source)
    },
    ExportAllDeclaration (node) {
      record(node.source)
    },
    ExportNamedDeclaration (node) {
      record(node.source)
    },
    ImportExpression (node) {
      if (node.source && node.source.type === 'Literal') {
        record(node.source)
      } else {
        console.warn(
          `[examples/build] Skipping non-literal dynamic import in ${filePath}`
        )
      }
    }
  })

  return imports
}

async function resolveModulePath (fromFile, specifier) {
  const baseDir = path.dirname(fromFile)
  const attempt = path.resolve(baseDir, specifier)
  const ext = path.extname(attempt)

  const candidates = []
  if (ext) {
    candidates.push(attempt)
    if (!ext) {
      candidates.push(`${attempt}.js`, `${attempt}.mjs`)
    }
  } else {
    candidates.push(attempt, `${attempt}.js`, `${attempt}.mjs`)
    candidates.push(
      path.join(attempt, 'index.js'),
      path.join(attempt, 'index.mjs')
    )
  }

  for (const candidate of candidates) {
    if (await pathExists(candidate)) return candidate
  }

  throw new Error(`Cannot resolve import "${specifier}" from ${fromFile}`)
}

function applyReplacements (source, replacements) {
  if (!replacements.length) return source
  const sorted = [...replacements].sort((a, b) => a.start - b.start)
  let output = ''
  let cursor = 0
  for (const rep of sorted) {
    output += source.slice(cursor, rep.start)
    output += JSON.stringify(rep.value)
    cursor = rep.end
  }
  output += source.slice(cursor)
  return output
}

async function bundleEntry ({ entryPath, entryOutPath, outputDir }) {
  const moduleMap = new Map()

  let moduleCounter = 0

  async function loadModule (filePath, { isEntry = false } = {}) {
    const absolute = path.resolve(filePath)
    if (moduleMap.has(absolute)) {
      const cached = moduleMap.get(absolute)
      if (isEntry) cached.isEntry = true
      return cached
    }

    const source = await fs.readFile(absolute, 'utf8')
    const moduleInfo = {
      id: moduleCounter++,
      path: absolute,
      source,
      isEntry,
      replacements: [],
      outPath: null
    }
    moduleMap.set(absolute, moduleInfo)

    const specifiers = collectModuleSpecifiers(source, absolute)
    for (const { node, specifier } of specifiers) {
      const resolved = await resolveModulePath(absolute, specifier)
      const depModule = await loadModule(resolved)
      moduleInfo.replacements.push({
        start: node.start,
        end: node.end,
        target: depModule
      })
    }

    return moduleInfo
  }

  await loadModule(entryPath, { isEntry: true })

  for (const moduleInfo of moduleMap.values()) {
    if (moduleInfo.isEntry) {
      moduleInfo.outPath = entryOutPath
    } else if (!moduleInfo.outPath) {
      moduleInfo.outPath = `modules/module-${moduleInfo.id}.js`
    }
  }

  for (const moduleInfo of moduleMap.values()) {
    if (!moduleInfo.replacements.length) {
      continue
    }
    const baseDir = path.posix.dirname(moduleInfo.outPath)
    const replacements = moduleInfo.replacements.map((rep) => {
      const targetOut = rep.target.outPath
      let relative = path.posix.relative(
        baseDir === '.' ? '.' : baseDir,
        targetOut
      )
      if (!relative.startsWith('.')) {
        relative = `./${relative}`
      }
      return {
        start: rep.start,
        end: rep.end,
        value: relative
      }
    })
    moduleInfo.code = applyReplacements(moduleInfo.source, replacements)
  }

  const emitted = new Set()

  for (const moduleInfo of moduleMap.values()) {
    const code = moduleInfo.code ?? moduleInfo.source
    const outPath = moduleInfo.outPath
    const destPath = path.join(outputDir, outPath.split('/').join(path.sep))
    await fs.mkdir(path.dirname(destPath), { recursive: true })
    await fs.writeFile(destPath, code, 'utf8')
    emitted.add(outPath)
  }

  return emitted
}

async function processHtml (sourcePath, targetPath) {
  const html = await fs.readFile(sourcePath, 'utf8')
  const scriptRegex =
    /<script\b[^>]*type=["']module["'][^>]*src=["']([^"']+)["'][^>]*><\/script>/gi
  const entries = []
  let match
  while ((match = scriptRegex.exec(html)) !== null) {
    entries.push(match[1])
  }

  const rewritten = html.replace(/(\b(?:src|href)=["'])\.\.\/ui\//g, '$1./ui/')
  await fs.writeFile(targetPath, rewritten, 'utf8')

  return entries
}

async function copyExtraAssets (exampleName, destinationDir) {
  const assets = EXTRA_ASSETS.get(exampleName)
  if (!assets) return
  for (const asset of assets) {
    const spec =
      typeof asset === 'string' ? { source: asset, target: asset } : asset
    const from = path.join(EXAMPLES_ROOT, spec.source)
    if (!(await pathExists(from))) {
      console.warn(
        `[examples/build] Skipping missing extra asset ${spec.source}`
      )
      continue
    }
    const dest = path.join(
      destinationDir,
      spec.target.split('/').join(path.sep)
    )
    await fs.mkdir(path.dirname(dest), { recursive: true })
    await fs.copyFile(from, dest)
  }
}

async function copyExternalModules (exampleName, destinationDir) {
  const modules = EXTERNAL_NODE_MODULES.get(exampleName)
  if (!modules) return

  const sourceRoot = path.join(EXAMPLES_ROOT, 'node_modules')
  const targetRoot = path.join(destinationDir, 'node_modules')

  for (const specifier of modules) {
    const segments = specifier.split('/')
    const moduleSource = path.join(sourceRoot, ...segments)
    if (!(await pathExists(moduleSource))) {
      console.warn(
        `[examples/build] Missing external dependency ${specifier} for ${exampleName}`
      )
      continue
    }
    const moduleTarget = path.join(targetRoot, ...segments)
    await fs.mkdir(path.dirname(moduleTarget), { recursive: true })
    await fs.cp(moduleSource, moduleTarget, { recursive: true })
  }
}

async function buildExample (exampleName) {
  const sourceDir = path.join(EXAMPLES_ROOT, exampleName)
  const indexHtml = path.join(sourceDir, 'index.html')
  if (!(await pathExists(indexHtml))) return

  const outputDir = path.join(OUTPUT_ROOT, exampleName)
  await ensureEmptyDir(outputDir)

  const entries = await processHtml(
    indexHtml,
    path.join(outputDir, 'index.html')
  )

  if (!entries.length) {
    console.warn(
      `[examples/build] No module scripts found in ${exampleName}/index.html`
    )
  }

  const emittedFiles = new Set(['index.html'])

  for (const entry of entries) {
    const entryRel = entry.startsWith('./') ? entry.slice(2) : entry
    const entryFile = path.join(sourceDir, entryRel)
    if (!(await pathExists(entryFile))) {
      console.warn(
        `[examples/build] Entry script ${entry} in ${exampleName} not found`
      )
      continue
    }
    const entryOutPath = toPosix(entryRel || path.basename(entryFile))
    const emitted = await bundleEntry({
      entryPath: entryFile,
      entryOutPath,
      outputDir
    })
    for (const file of emitted) emittedFiles.add(file)
  }

  await copyStaticAssets(sourceDir, outputDir, { skip: emittedFiles })

  const uiCssSource = path.join(EXAMPLES_ROOT, 'ui', 'styles.css')
  if (await pathExists(uiCssSource)) {
    const uiTargetDir = path.join(outputDir, 'ui')
    await fs.mkdir(uiTargetDir, { recursive: true })
    await fs.copyFile(uiCssSource, path.join(uiTargetDir, 'styles.css'))
  }

  await copyExtraAssets(exampleName, outputDir)
  await copyExternalModules(exampleName, outputDir)
}

async function main () {
  await ensureEmptyDir(OUTPUT_ROOT)
  const entries = await fs.readdir(EXAMPLES_ROOT, { withFileTypes: true })
  const exampleDirs = entries
    .filter((entry) => entry.isDirectory() && !IGNORE_DIRS.has(entry.name))
    .map((entry) => entry.name)

  for (const exampleName of exampleDirs) {
    const indexHtml = path.join(EXAMPLES_ROOT, exampleName, 'index.html')
    if (!(await pathExists(indexHtml))) continue
    await buildExample(exampleName)
    console.log(`Built example: ${exampleName}`)
  }
}

if (import.meta.url === `file://${process.argv[1]}`) {
  main().catch((err) => {
    console.error('[examples/build] Build failed:', err)
    process.exitCode = 1
  })
}
