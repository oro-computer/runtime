import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, symlinkSync, writeFileSync, existsSync, readFileSync, realpathSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { spawnSync } from 'node:child_process'

test('development API refresh preflights every existing entry before adding new links', () => {
  const root = mkdtempSync(path.join(tmpdir(), 'oro-api-link-'))
  const source = path.join(root, 'source')
  const target = path.join(root, 'target')
  mkdirSync(source)
  mkdirSync(target)
  for (const name of ['a-new.js', 'window.js', 'z-conflict.js', 'global.d.ts']) writeFileSync(path.join(source, name), 'source')
  symlinkSync(path.join(source, 'window.js'), path.join(target, 'window.js'))
  writeFileSync(path.join(target, 'z-conflict.js'), 'preserve')
  const helper = fileURLToPath(new URL('../../bin/link-runtime-api.js', import.meta.url))
  const rejected = spawnSync(process.execPath, [helper, source, target], { encoding: 'utf8' })
  assert.notEqual(rejected.status, 0)
  assert.equal(existsSync(path.join(target, 'a-new.js')), false)
  assert.equal(readFileSync(path.join(target, 'z-conflict.js'), 'utf8'), 'preserve')
  const linked = path.join(root, 'linked')
  mkdirSync(linked)
  symlinkSync(path.join(source, 'window.js'), path.join(linked, 'window.js'))
  const refreshed = spawnSync(process.execPath, [helper, source, linked], { encoding: 'utf8' })
  assert.equal(refreshed.status, 0, refreshed.stderr)
  assert.equal(realpathSync(path.join(linked, 'a-new.js')), path.join(source, 'a-new.js'))
  assert.equal(existsSync(path.join(linked, 'global.d.ts')), false)
  const repeated = spawnSync(process.execPath, [helper, source, linked], { encoding: 'utf8' })
  assert.equal(repeated.status, 0, repeated.stderr)
  assert.match(repeated.stdout, /Added 0/)
})
