#!/usr/bin/env node
import fs from 'node:fs/promises'
import path from 'node:path'
import {
  generateApiModuleDoc,
  generateApiModuleManpage
} from './docs-generator/api-module.js'
import {
  generateCApiManpage,
  PUBLIC_C_API_HEADERS
} from './docs-generator/c-api.js'
import { generateIpcManpages } from './docs-generator/ipc-manual.js'
import { generateConfig } from './docs-generator/config.js'
import { generateCli, generateCliManpages } from './docs-generator/cli.js'

const RAW_VERSION = (await fs.readFile('./VERSION.txt', 'utf8')).trim()
const CLI_NAME = (process.env.DOCS_CLI_NAME ?? 'oroc').trim()

const JS_INTERFACE_DIR = 'api'
const RUNTIME_NODE_DIR = 'npm/packages/@oro-computer/runtime-node'
const CLI_MANPAGE_DIR = 'share/man/man1'
const API_MANPAGE_DIR = 'share/man/man3'
const GUIDE_MANPAGE_DIR = 'share/man/man7'

async function writeTextFile (destFile, content) {
  await fs.writeFile(destFile, `${content.trim()}\n`)
}

async function removeGeneratedPages (destDir, matcher) {
  try {
    const entries = await fs.readdir(destDir, { withFileTypes: true })
    await Promise.all(
      entries
        .filter((entry) => entry.isFile() && matcher(entry.name))
        .map((entry) => fs.unlink(path.join(destDir, entry.name)))
    )
  } catch (error) {
    if (error?.code !== 'ENOENT') {
      throw error
    }
  }
}

function assertUniqueManpageFilenames (manpages, section) {
  const seen = new Set()
  const duplicates = new Set()

  for (const { filename } of manpages) {
    if (seen.has(filename)) {
      duplicates.add(filename)
    }
    seen.add(filename)
  }

  if (duplicates.size > 0) {
    throw new Error(
      `Duplicate generated section ${section} manpage filenames: ${[
        ...duplicates
      ].join(', ')}`
    )
  }
}

async function listFilesRecursive (dir) {
  const entries = await fs.readdir(dir, { withFileTypes: true })
  const files = await Promise.all(
    entries.map(async (entry) => {
      const resolved = path.join(dir, entry.name)
      if (entry.isDirectory()) {
        return listFilesRecursive(resolved)
      }

      return [resolved]
    })
  )

  return files.flat()
}

function normalizeApiModuleLocation (location) {
  return String(location).replace(/\\/g, '/')
}

function canonicalApiModuleSpecifier (location, knownLocations) {
  let relative = normalizeApiModuleLocation(location)
    .replace(/^api\//, '')
    .replace(/\.js$/, '')

  if (relative.endsWith('/index')) {
    const aliasLocation = `api/${relative.slice(0, -'/index'.length)}.js`
    if (knownLocations.has(aliasLocation)) {
      relative = relative.slice(0, -'/index'.length)
    }
  }

  return `oro:${relative}`
}

function shouldPreferManpageSource (candidate, current) {
  const candidateLocation = normalizeApiModuleLocation(candidate)
  const currentLocation = normalizeApiModuleLocation(current)
  const candidateIsIndex = candidateLocation.endsWith('/index.js')
  const currentIsIndex = currentLocation.endsWith('/index.js')

  if (candidateIsIndex !== currentIsIndex) {
    return candidateIsIndex
  }

  return candidateLocation.localeCompare(currentLocation) < 0
}

function collectCanonicalApiManpageInputs (apiModules) {
  const knownLocations = new Set(apiModules.map(normalizeApiModuleLocation))
  const selected = new Map()

  for (const location of apiModules.map(normalizeApiModuleLocation)) {
    const moduleSpecifier = canonicalApiModuleSpecifier(
      location,
      knownLocations
    )
    const existing = selected.get(moduleSpecifier)

    if (!existing || shouldPreferManpageSource(location, existing.location)) {
      selected.set(moduleSpecifier, { location, moduleSpecifier })
    }
  }

  return [...selected.values()].sort((a, b) =>
    a.moduleSpecifier.localeCompare(b.moduleSpecifier)
  )
}

{
  const modules = [
    'application.js',
    // 'bootstrap.js', // don't document this module yet
    'crypto.js',
    'dgram.js',
    'dns/index.js',
    'dns/promises.js',
    'fs/index.js',
    'fs/promises.js',
    'ipc.js',
    // 'location.js',
    'network.js',
    'os.js',
    'path/path.js',
    'process.js',
    'test/index.js',
    // 'test/dom-helpers.js',
    // 'url/index.js',
    'window.js'
  ]

  const dest = JS_INTERFACE_DIR
  const md = 'README.md'

  const chunks = await Promise.all(
    modules.map(async (module) => {
      const location = `${dest}/${module}`
      const src = await fs.readFile(path.relative(process.cwd(), location))
      return generateApiModuleDoc({ src, location })
    })
  )

  // modules special with special handling
  chunks.push(
    {
      header: 'Buffer',
      content: `
# Buffer

Buffer module is a [third party](https://github.com/feross/buffer) vendor module provided by Feross Aboukhadijeh and other contributors (MIT License).

External docs: https://nodejs.org/api/buffer.html
`
    },
    {
      header: 'Events',
      content: `
# Events

Events module is a [third party](https://github.com/browserify/events/blob/main/events.js) module provided by Browserify and Node.js contributors (MIT License).

External docs: https://nodejs.org/api/events.html
`
    }
  )

  const result = chunks
    // sort by header
    .sort((a, b) => (a.header > b.header ? 1 : -1))
    // get content
    .map((chunk) => chunk.content)
    // join
    .join('\n')

  const destFile = path.relative(process.cwd(), `${dest}/${md}`)
  await writeTextFile(destFile, result)
}

// runtime-node/API.md
{
  const filename = 'index.js'
  const dest = RUNTIME_NODE_DIR
  const location = `${dest}/${filename}`
  const md = 'API.md'

  const srcFile = path.relative(process.cwd(), location)
  const src = await fs.readFile(srcFile)

  const { content } = generateApiModuleDoc({ src, location })

  const destFile = path.relative(process.cwd(), `${dest}/${md}`)
  await writeTextFile(destFile, content)
}

const templateFilePath = path.relative(process.cwd(), 'src/cli/templates.hh')
const templateFileSource = await fs.readFile(templateFilePath, 'utf8')
const templateDocSource = templateFileSource
  .replace(/{{cli_name}}/g, CLI_NAME)
  .replace(/{{cli_version}}/g, RAW_VERSION)

{
  const config = generateConfig(templateDocSource)
  await writeTextFile('api/CONFIG.md', config)
}

{
  const cli = generateCli(templateDocSource)
  await writeTextFile('api/CLI.md', cli)
}

{
  const manpages = generateCliManpages(templateDocSource, {
    cliVersion: RAW_VERSION
  })
  await fs.mkdir(CLI_MANPAGE_DIR, { recursive: true })
  await removeGeneratedPages(CLI_MANPAGE_DIR, (name) =>
    /^oroc.*\.1$/.test(name)
  )

  await Promise.all(
    manpages.map(({ filename, content }) =>
      writeTextFile(path.join(CLI_MANPAGE_DIR, filename), content)
    )
  )
}

{
  const apiModules = (await listFilesRecursive(JS_INTERFACE_DIR))
    .filter((filename) => filename.endsWith('.js'))
    .sort((a, b) => a.localeCompare(b))
  const canonicalApiModules = collectCanonicalApiManpageInputs(apiModules)

  const moduleManpages = await Promise.all(
    canonicalApiModules.map(async ({ location, moduleSpecifier }) => {
      const src = await fs.readFile(location)
      return generateApiModuleManpage({
        src,
        location,
        moduleSpecifier
      })
    })
  )

  const cApiManpages = await Promise.all(
    PUBLIC_C_API_HEADERS.map(async (location) => {
      const source = await fs.readFile(location, 'utf8')
      return generateCApiManpage({
        source,
        location
      })
    })
  )

  const section3Manpages = [...moduleManpages, ...cApiManpages]
  assertUniqueManpageFilenames(section3Manpages, 3)

  await fs.mkdir(API_MANPAGE_DIR, { recursive: true })
  await removeGeneratedPages(API_MANPAGE_DIR, (name) => /^oro.*\.3$/.test(name))

  await Promise.all(
    section3Manpages.map(({ filename, content }) =>
      writeTextFile(path.join(API_MANPAGE_DIR, filename), content)
    )
  )

  const ipcGuides = generateIpcManpages(
    await Promise.all(
      apiModules.map(async (location) => ({
        location,
        source: await fs.readFile(location, 'utf8')
      }))
    )
  )

  await fs.mkdir(GUIDE_MANPAGE_DIR, { recursive: true })
  await removeGeneratedPages(GUIDE_MANPAGE_DIR, (name) =>
    /^oro.*\.7$/.test(name)
  )

  await Promise.all(
    ipcGuides.map(({ filename, content }) =>
      writeTextFile(path.join(GUIDE_MANPAGE_DIR, filename), content)
    )
  )
}
