import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { runDesktopTests } from '../scripts/test-desktop.js'

// Execute the real staging and strict-pass flow; only the native CLI is replaced.
test('isolated and strict desktop passes preserve native source roots and apply lifecycle policy at the config root', { skip: process.platform === 'win32' }, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro desktop staging '))
  const record = path.join(directory, 'calls.jsonl')
  const binary = path.join(directory, 'public-oroc.cjs')
  fs.writeFileSync(binary, `#!${process.execPath}
const fs = require('node:fs')
const path = require('node:path')
fs.appendFileSync(process.env.ORO_STAGING_RECORD, JSON.stringify({ cwd: process.cwd(), config: fs.readFileSync('oro.toml', 'utf8'), args: process.argv.slice(2) }) + '\\n')
`, { mode: 0o700 })
  const result = runDesktopTests({
    isolate: true,
    strict: true,
    tmpdir: directory,
    keepWorkdir: true,
    env: { ...process.env, ORO_BIN: binary, ORO_STAGING_RECORD: record }
  })
  assert.equal(result, 0)
  const calls = fs.readFileSync(record, 'utf8').trim().split('\n').map(line => JSON.parse(line))
  assert.equal(calls.length, 2)
  const native = fileURLToPath(new URL('../src/runtime-core', import.meta.url)).split(path.sep).join('/')
  for (const call of calls) assert.ok(call.config.includes('runtime-core-tests = ' + JSON.stringify(native)))
  assert.equal(calls[1].config.split('\n')[0], 'lifecycle_desktop_always_running = false')
  assert.match(calls[1].config, /\[test-section\.subsection\]\nkey = "value"/)
  assert.notEqual(calls[0].cwd, calls[1].cwd)
  assert.equal(calls[0].args.includes('--env=ORO_LIFECYCLE_TEST_STRICT=1'), false)
  assert.ok(calls[1].args.includes('--env=ORO_LIFECYCLE_TEST_STRICT=1'))
  assert.ok(calls[1].args.includes('lifecycle/strict.js'))
  assert.match(calls[1].config, /env = \[\n {2}"ORO_LIFECYCLE_TEST_STRICT",/)
})
