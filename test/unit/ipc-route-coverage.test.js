import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync, readdirSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(__dirname, '..', '..')

function walk (directory, extension) {
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const filename = path.join(directory, entry.name)
    if (entry.isDirectory()) return walk(filename, extension)
    return entry.name.endsWith(extension) ? [filename] : []
  })
}

test('literal public IPC calls have native route handlers', () => {
  const calls = new Map()
  const callPattern =
    /\bipc\s*\.\s*(?:send|sendSync|request|write)\s*\(\s*['"]([^'"]+)['"]/g
  const routeMapPattern = /const ROUTES\s*=\s*(?:Object\.freeze\s*\()?\s*\{([\s\S]*?)\}\s*\)?/g
  const routeValuePattern = /:\s*['"]([^'"]+)['"]/g

  for (const filename of walk(path.join(repoRoot, 'api'), '.js')) {
    const source = readFileSync(filename, 'utf8')
    for (const match of source.matchAll(callPattern)) {
      const route = match[1].toLowerCase()
      if (!calls.has(route)) calls.set(route, new Set())
      calls.get(route).add(path.relative(repoRoot, filename))
    }
    for (const routeMap of source.matchAll(routeMapPattern)) {
      for (const match of routeMap[1].matchAll(routeValuePattern)) {
        const route = match[1].toLowerCase()
        if (!calls.has(route)) calls.set(route, new Set())
        calls.get(route).add(path.relative(repoRoot, filename))
      }
    }
  }

  const routes = new Set()
  const routePattern = /(?:router->|router\.)map\s*\(\s*"([^"]+)"/g
  for (const filename of walk(path.join(repoRoot, 'src'), '.cc')) {
    const source = readFileSync(filename, 'utf8')
    for (const match of source.matchAll(routePattern)) {
      routes.add(match[1].toLowerCase())
    }
  }

  const desktopMain = readFileSync(
    path.join(repoRoot, 'src', 'desktop', 'main.cc'),
    'utf8'
  )
  for (const route of ['process.open', 'process.write', 'process.kill']) {
    assert.match(
      desktopMain,
      new RegExp(`message\\.name == "${route.replace('.', '\\.')}"`),
      `${route} should remain handled by the desktop backend bridge`
    )
    routes.add(route)
  }

  const missing = [...calls]
    .filter(([route]) => !routes.has(route))
    .map(([route, origins]) => `${route} (${[...origins].join(', ')})`)

  assert.deepEqual(missing, [], `Missing native IPC routes:\n${missing.join('\n')}`)
})

test('queued IPC events preserve payload sources and dispatch on the UI loop', () => {
  const routes = readFileSync(
    path.join(repoRoot, 'src', 'runtime', 'ipc', 'routes.cc'),
    'utf8'
  )
  assert.match(
    routes,
    /normalizeResultSourceFromPayload\(result\)/,
    'core callbacks should preserve the source declared by queued payloads'
  )

  const router = readFileSync(
    path.join(repoRoot, 'src', 'runtime', 'ipc', 'router.cc'),
    'utf8'
  )
  assert.match(
    router,
    /if \(result\.seq == "-1"\) \{\s+this->dispatcher\.dispatch/,
    'queued responses should enter the UI dispatcher before bridge delivery'
  )
})
