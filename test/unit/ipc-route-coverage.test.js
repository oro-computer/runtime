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

test('IPC streams deliver data before the producer finishes', () => {
  const bridge = readFileSync(
    path.join(repoRoot, 'src', 'runtime', 'bridge', 'bridge.cc'),
    'utf8'
  )
  const routes = readFileSync(
    path.join(repoRoot, 'src', 'runtime', 'ipc', 'routes.cc'),
    'utf8'
  )
  const schemeHandlers = readFileSync(
    path.join(repoRoot, 'src', 'runtime', 'webview', 'scheme_handlers.cc'),
    'utf8'
  )

  assert.match(
    bridge,
    /event\.count\(\) > 0\) \{\s+response->write\(event\.str\(\)\);/,
    'SSE events should be written as they are emitted'
  )
  assert.match(
    bridge,
    /if \(chunk && size > 0\) \{\s+response->write\(size, chunk\);/,
    'binary chunks should be written as they are emitted'
  )
  assert.doesNotMatch(
    bridge,
    /kFlushThreshold|kChunkFlushThreshold/,
    'small streams should not remain buffered until completion'
  )
  assert.match(
    routes,
    /router->map\("diagnostics\.stream\.sse"[\s\S]*eventStreamCallback = stream;/,
    'the documented diagnostic SSE route should register a stream producer'
  )
  assert.match(
    routes,
    /router->map\("diagnostics\.stream\.chunks"[\s\S]*chunkStreamCallback = stream;/,
    'the documented diagnostic chunk route should register a stream producer'
  )
  assert.match(
    routes,
    /kMaxDiagnosticStreamItems = 256[\s\S]*kMaxDiagnosticStreamBytes = 4 \* 1024 \* 1024/,
    'diagnostic stream allocation and duration inputs should remain bounded'
  )
  assert.match(
    bridge,
    /eventStreamCallback = \[.*[\s\S]*streamStartCallback\(\);[\s\S]*chunkStreamCallback = \[.*[\s\S]*streamStartCallback\(\);/,
    'custom-scheme producers should start only after their transport callbacks are installed'
  )
  const streamBridge = bridge.match(
    /\/\/ handle event source streams([\s\S]*?)if \(result\.queuedResponse\.body != nullptr\)/
  )?.[1]
  assert.ok(streamBridge, 'the custom-scheme stream bridge should exist')
  assert.equal(
    (streamBridge.match(/response->writeHead\(200\)/g) || []).length,
    2,
    'each streaming response should write its headers exactly once'
  )
  assert.match(
    streamBridge,
    /response->writeHead\(200\)[\s\S]*eventStreamCallback = \[[\s\S]*content-type[\s\S]*application\/octet-stream[\s\S]*response->writeHead\(200\)[\s\S]*chunkStreamCallback = \[/,
    'complete streaming response headers should be written before each producer callback is installed'
  )
  assert.match(
    schemeHandlers,
    /if \(isChunked \|\| isSSE\) \{\s+webkit_uri_scheme_request_finish_with_response\([\s\S]*platformResponseStarted = true;/,
    'Linux WebKit should receive streaming response headers before the producer finishes'
  )
  assert.match(
    schemeHandlers,
    /const bool requestActive =[\s\S]*if \(requestActive && !this->platformResponseStarted\)[\s\S]*g_output_stream_close[\s\S]*g_object_unref\(this->platformResponse\);/,
    'cancelled Linux streams should close and release their pipe without finishing the request twice'
  )
  assert.match(
    schemeHandlers,
    /DeleteGlobalRef\(this->platformResponse\);\s+this->platformResponse = nullptr;/,
    'Android responses should release their global reference before clearing it'
  )
})
