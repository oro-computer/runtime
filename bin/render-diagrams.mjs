#!/usr/bin/env node

// Render all Mermaid diagrams embedded in Markdown files under `docs/`.
// - Extracts ```mermaid fences from each .md
// - Writes .mmd sources under build/diagrams/<doc>/<NN>.mmd
// - Invokes Mermaid CLI (mmdc) to render SVG (default) or PNG
//
// Usage:
//   npm run docs:diagrams            # render to SVG
//   npm run docs:diagrams:png        # render to PNG
//   node bin/render-diagrams.mjs --format=svg --out=build/diagrams

import { promises as fs } from 'node:fs'
import { spawn } from 'node:child_process'
import path from 'node:path'
import process from 'node:process'

const DOCS_DIR = path.join(process.cwd(), 'docs')
const OUT_DIR_DEFAULT = path.join(process.cwd(), 'build', 'diagrams')

function parseArgs (argv) {
  const args = { format: 'svg', outDir: OUT_DIR_DEFAULT }
  for (const arg of argv.slice(2)) {
    if (arg.startsWith('--format=')) args.format = arg.split('=')[1]
    else if (arg.startsWith('--out=')) {
      args.outDir = path.resolve(arg.split('=')[1])
    }
  }
  if (!['svg', 'png'].includes(args.format)) args.format = 'svg'
  return args
}

async function ensureDir (dir) {
  await fs.mkdir(dir, { recursive: true })
}

async function exists (p) {
  try {
    await fs.access(p)
    return true
  } catch {
    return false
  }
}

async function walk (dir, acc = []) {
  const entries = await fs.readdir(dir, { withFileTypes: true })
  for (const e of entries) {
    const p = path.join(dir, e.name)
    if (e.isDirectory()) await walk(p, acc)
    else if (e.isFile() && p.endsWith('.md')) acc.push(p)
  }
  return acc
}

function extractMermaidBlocks (markdown) {
  const blocks = []
  const re = /```mermaid\r?\n([\s\S]*?)```/g
  let m
  while ((m = re.exec(markdown)) !== null) {
    const source = (m[1] || '').trim()
    if (source) blocks.push(source)
  }
  return blocks
}

async function resolveMmdc () {
  // Prefer a locally installed binary
  const local = path.join(process.cwd(), 'node_modules', '.bin', 'mmdc')
  const localWin = path.join(process.cwd(), 'node_modules', '.bin', 'mmdc.cmd')
  if (await exists(local)) return { cmd: local, mode: 'direct' }
  if (await exists(localWin)) return { cmd: localWin, mode: 'direct' }
  // Fallback to npx without install
  return { cmd: 'npx', mode: 'npx' }
}

async function renderWithMmdc ({ mmdc, input, output, format }) {
  return await new Promise((resolve) => {
    const args =
      mmdc.mode === 'direct'
        ? ['-i', input, '-o', output, '-b', 'transparent']
        : [
            '--no-install',
            '@mermaid-js/mermaid-cli',
            '-i',
            input,
            '-o',
            output,
            '-b',
            'transparent'
          ]
    if (format === 'png') args.push('-e', 'png')

    const child = spawn(mmdc.cmd, args, { stdio: 'inherit' })
    child.on('exit', (code) => resolve(code === 0))
  })
}

async function main () {
  const { format, outDir } = parseArgs(process.argv)
  const docsExists = await exists(DOCS_DIR)
  if (!docsExists) {
    console.error('docs/ not found — nothing to render')
    process.exit(0)
  }

  await ensureDir(outDir)
  const files = await walk(DOCS_DIR)
  const mmdc = await resolveMmdc()

  let totalBlocks = 0
  let rendered = 0

  for (const file of files) {
    const rel = path.relative(DOCS_DIR, file)
    const base = path.basename(file, path.extname(file))
    const groupDir = path.join(outDir, path.dirname(rel), base)
    await ensureDir(groupDir)

    const text = await fs.readFile(file, 'utf8')
    const blocks = extractMermaidBlocks(text)
    if (blocks.length === 0) continue
    totalBlocks += blocks.length

    for (let i = 0; i < blocks.length; i++) {
      const idx = String(i + 1).padStart(2, '0')
      const mmdPath = path.join(groupDir, `${idx}.mmd`)
      const outPath = path.join(groupDir, `${idx}.${format}`)
      await fs.writeFile(mmdPath, blocks[i] + '\n', 'utf8')

      const ok = await renderWithMmdc({
        mmdc,
        input: mmdPath,
        output: outPath,
        format
      })
      if (ok) rendered++
      else {
        console.error('Failed to render:', mmdPath)
        if (mmdc.mode === 'npx') {
          console.error('Hint: install Mermaid CLI locally:')
          console.error('  npm i -D @mermaid-js/mermaid-cli')
          console.error('Then re-run: npm run docs:diagrams')
        }
      }
    }
  }

  console.log(
    `Extracted ${totalBlocks} diagram(s); rendered ${rendered}. Output: ${outDir}`
  )
}

main().catch((err) => {
  console.error(err && err.stack ? err.stack : err)
  process.exit(1)
})
