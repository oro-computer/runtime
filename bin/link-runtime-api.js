#!/usr/bin/env node
// Add missing entries to an existing development-linked API package without replacing files.
import assert from 'node:assert/strict'
import fs from 'node:fs'
import path from 'node:path'

const [source, destination] = process.argv.slice(2).map(value => path.resolve(value))
assert(source && destination, 'Usage: link-runtime-api.js <api-source> <linked-package>')
assert.equal(fs.realpathSync(path.join(destination, 'window.js')), fs.realpathSync(path.join(source, 'window.js')),
  'Target must already be a development-linked runtime API package')
const entries = fs.readdirSync(source).filter(name => name !== 'global.d.ts')
const missing = []
for (const name of entries) {
  const target = path.join(destination, name)
  try {
    const existing = fs.lstatSync(target)
    assert(existing.isSymbolicLink(), `Refusing to replace an existing package file: ${name}`)
    assert.equal(fs.realpathSync(target), fs.realpathSync(path.join(source, name)), `Conflicting API link: ${name}`)
  } catch (error) {
    if (error.code !== 'ENOENT') throw error
    // A dangling link is existing state too; never replace it.
    assert(!fs.existsSync(path.dirname(target)) || !fs.readdirSync(path.dirname(target)).includes(name), `Dangling API link: ${name}`)
    missing.push(name)
  }
}
for (const name of missing) {
  fs.symlinkSync(path.join(source, name), path.join(destination, name),
    fs.statSync(path.join(source, name)).isDirectory() ? 'dir' : 'file')
}
process.stdout.write(`Added ${missing.length} missing API links; existing entries preserved.\n`)
