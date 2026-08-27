#!/usr/bin/env node

/**
 * MCP specification fetcher/indexer for the Oro Runtime repository.
 *
 * Usage:
 *   node download-mcp-spec.js                    # index the current supported spec
 *   node download-mcp-spec.js --fetch            # fetch remote pages and write index
 *   node download-mcp-spec.js --version=2025-06-18 # index a compatibility spec
 *
 * Files are stored under docs/mcp/<version>/raw/. Existing downloads are not
 * replaced unless --force is provided.
 */

import fs from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const BASE_URL = 'https://modelcontextprotocol.io'
const DEFAULT_SPEC_VERSION = '2026-07-28'
const args = process.argv.slice(2)
const versionArg = args.find((arg) => arg.startsWith('--version='))
const SPEC_VERSION = versionArg
  ? versionArg.slice('--version='.length)
  : process.env.MCP_SPEC_VERSION || DEFAULT_SPEC_VERSION

if (!/^\d{4}-\d{2}-\d{2}$/.test(SPEC_VERSION)) {
  throw new TypeError(`Invalid MCP specification version: ${SPEC_VERSION}`)
}

const SPEC_BASE_PATH = `/specification/${SPEC_VERSION}`

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)

const OUTPUT_ROOT = path.resolve(__dirname, SPEC_VERSION)
const RAW_ROOT = path.join(OUTPUT_ROOT, 'raw')
const INDEX_JSON = path.join(OUTPUT_ROOT, 'spec-index.json')
const INDEX_MD = path.join(OUTPUT_ROOT, 'INDEX.md')

const pages = [
  {
    id: 'overview',
    remotePath: '',
    title: 'Model Context Protocol Specification',
    category: 'Overview'
  },
  {
    id: 'key-changes',
    remotePath: 'key-changes',
    title: 'Key Changes',
    category: 'Overview'
  },
  {
    id: 'architecture',
    remotePath: 'architecture',
    title: 'Architecture',
    category: 'Overview'
  },

  {
    id: 'base-overview',
    remotePath: 'basic',
    title: 'Base Protocol Overview',
    category: 'Base Protocol'
  },
  {
    id: 'base-lifecycle',
    remotePath: 'basic/lifecycle',
    title: 'Lifecycle',
    category: 'Base Protocol'
  },
  {
    id: 'base-transports',
    remotePath: 'basic/transports',
    title: 'Transports',
    category: 'Base Protocol'
  },
  {
    id: 'base-authorization',
    remotePath: 'basic/authorization',
    title: 'Authorization',
    category: 'Base Protocol'
  },
  {
    id: 'base-security',
    remotePath: 'basic/security-best-practices',
    title: 'Security Best Practices',
    category: 'Base Protocol'
  },
  {
    id: 'utility-cancellation',
    remotePath: 'basic/utilities/cancellation',
    title: 'Cancellation Utility',
    category: 'Base Protocol Utilities'
  },
  {
    id: 'utility-ping',
    remotePath: 'basic/utilities/ping',
    title: 'Ping Utility',
    category: 'Base Protocol Utilities'
  },
  {
    id: 'utility-progress',
    remotePath: 'basic/utilities/progress',
    title: 'Progress Utility',
    category: 'Base Protocol Utilities'
  },

  {
    id: 'client-roots',
    remotePath: 'client/roots',
    title: 'Roots',
    category: 'Client Features'
  },
  {
    id: 'client-sampling',
    remotePath: 'client/sampling',
    title: 'Sampling',
    category: 'Client Features'
  },
  {
    id: 'client-elicitation',
    remotePath: 'client/elicitation',
    title: 'Elicitation',
    category: 'Client Features'
  },

  {
    id: 'server-overview',
    remotePath: 'server',
    title: 'Server Features Overview',
    category: 'Server Features'
  },
  {
    id: 'server-prompts',
    remotePath: 'server/prompts',
    title: 'Prompts',
    category: 'Server Features'
  },
  {
    id: 'server-resources',
    remotePath: 'server/resources',
    title: 'Resources',
    category: 'Server Features'
  },
  {
    id: 'server-tools',
    remotePath: 'server/tools',
    title: 'Tools',
    category: 'Server Features'
  },
  {
    id: 'server-logging',
    remotePath: 'server/utilities/logging',
    title: 'Logging Utility',
    category: 'Server Utilities'
  },
  {
    id: 'server-completion',
    remotePath: 'server/utilities/completion',
    title: 'Completion Utility',
    category: 'Server Utilities'
  },

  {
    id: 'schema',
    remotePath: 'schema',
    title: 'Schema Reference',
    category: 'Schema'
  }
]

const shouldFetch = args.includes('--fetch')
const forceFetch = args.includes('--force')
const verbose = args.includes('--verbose') || shouldFetch

async function ensureOutputDirs () {
  await fs.mkdir(RAW_ROOT, { recursive: true })
}

function resolveRemoteUrl (remotePath) {
  const normalized = remotePath ? `/${remotePath.replace(/^\/|\/$/g, '')}` : ''
  return new URL(`${SPEC_BASE_PATH}${normalized}`, BASE_URL).toString()
}

function resolveLocalPaths (remotePath) {
  const normalized = remotePath.replace(/^\/|\/$/g, '')
  const segments = normalized ? normalized.split('/') : []
  let fileName = segments.pop()
  if (!fileName) fileName = 'index'
  const dirPath = path.join(RAW_ROOT, ...segments)
  const filePath = path.join(dirPath, `${fileName}.html`)
  const relative = path.posix.join('raw', ...segments, `${fileName}.html`)
  return { dirPath, filePath, relative }
}

async function fetchPage (page) {
  const url = resolveRemoteUrl(page.remotePath)
  const { dirPath, filePath, relative } = resolveLocalPaths(page.remotePath)
  await fs.mkdir(dirPath, { recursive: true })

  if (!shouldFetch) {
    if (verbose) console.log(`[dry-run] ${page.title} -> ${relative}`)
    return { status: 'skipped', url, local: relative }
  }

  if (!forceFetch) {
    try {
      await fs.access(filePath)
      if (verbose) console.log(`[cached] ${page.title} -> ${relative}`)
      return { status: 'cached', url, local: relative }
    } catch {
      // continue to download
    }
  }

  if (verbose) console.log(`[fetch] ${page.title} <- ${url}`)
  const response = await fetch(url, {
    headers: {
      'User-Agent': 'oro-runtime-mcp-spec-fetcher/0.1 (+https://oro.computer)'
    }
  })

  if (!response.ok) {
    return {
      status: 'error',
      url,
      local: relative,
      error: `${response.status} ${response.statusText}`
    }
  }

  const body = await response.text()
  await fs.writeFile(filePath, body, 'utf8')
  return { status: 'downloaded', url, local: relative, bytes: body.length }
}

async function writeIndex (results) {
  const indexData = {
    version: SPEC_VERSION,
    baseUrl: new URL(`${SPEC_BASE_PATH}/`, BASE_URL).toString(),
    generatedAt: new Date().toISOString(),
    pages: pages.map((page) => {
      const result = results.get(page.id) || {}
      const { relative } = resolveLocalPaths(page.remotePath)
      return {
        id: page.id,
        title: page.title,
        category: page.category,
        remote: resolveRemoteUrl(page.remotePath),
        local: relative,
        status: result.status || 'unknown',
        bytes: result.bytes || null,
        error: result.error || null
      }
    })
  }

  await fs.writeFile(
    INDEX_JSON,
    `${JSON.stringify(indexData, null, 2)}\n`,
    'utf8'
  )

  const markdownLines = []
  markdownLines.push(`# MCP Specification ${SPEC_VERSION}`)
  markdownLines.push('')
  markdownLines.push(`Base URL: ${indexData.baseUrl}`)
  markdownLines.push('')

  const categories = [...new Set(pages.map((page) => page.category))]
  for (const category of categories) {
    markdownLines.push(`## ${category}`)
    markdownLines.push('')
    markdownLines.push('| Title | Remote | Local | Status |')
    markdownLines.push('| --- | --- | --- | --- |')
    for (const page of pages.filter((p) => p.category === category)) {
      const result = results.get(page.id) || {}
      const { relative } = resolveLocalPaths(page.remotePath)
      const remoteUrl = resolveRemoteUrl(page.remotePath)
      const status = result.status || 'unknown'
      markdownLines.push(
        `| ${page.title} | ${remoteUrl} | ${relative} | ${status} |`
      )
    }
    markdownLines.push('')
  }

  await fs.writeFile(INDEX_MD, `${markdownLines.join('\n')}\n`, 'utf8')
}

async function main () {
  await ensureOutputDirs()

  const results = new Map()
  for (const page of pages) {
    try {
      const result = await fetchPage(page)
      results.set(page.id, result)
    } catch (err) {
      results.set(page.id, { status: 'error', error: err.message })
      console.error(`Failed to process ${page.title}: ${err.message}`)
    }
  }

  await writeIndex(results)

  const summary = {
    downloaded: [...results.values()].filter((r) => r.status === 'downloaded')
      .length,
    cached: [...results.values()].filter((r) => r.status === 'cached').length,
    skipped: [...results.values()].filter((r) => r.status === 'skipped').length,
    errors: [...results.values()].filter((r) => r.status === 'error').length
  }

  console.log(
    `\nDone. downloaded=${summary.downloaded} cached=${summary.cached} skipped=${summary.skipped} errors=${summary.errors}`
  )
  if (!shouldFetch) {
    console.log(
      'Run with --fetch to download the MCP specification HTML locally.'
    )
  } else if (summary.errors > 0) {
    console.log(
      'Some pages failed to download; check spec-index.json for details.'
    )
  }
}

try {
  await main()
} catch (err) {
  console.error(err)
  process.exitCode = 1
}
