import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(__dirname, '..', '..')
const templates = readFileSync(
  path.join(repoRoot, 'src/cli/templates.hh'),
  'utf8'
)

test('hello world scaffold prefers oro module specifiers', () => {
  assert.match(templates, /connect-src oro:/, 'CSP allows oro scheme')
  assert.match(
    templates,
    /import process from 'oro:process'/,
    'JS templates import via oro scheme'
  )
  assert.match(
    templates,
    /constexpr auto gHelloWorldServiceWorker = R"JavaScript\(import process from 'oro:process'/,
    'service worker stub uses oro scheme'
  )
})

test('default config banner references Oro Runtime', () => {
  assert.match(
    templates,
    /^# Oro Runtime ☆ v\{\{cli_version\}\}$/m,
    'default config comment uses Oro name'
  )
})
