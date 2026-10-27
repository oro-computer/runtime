'use strict'

const fs = require('node:fs')
const path = require('node:path')
const crypto = require('node:crypto')

/**
 * Watch declared inputs and dependency outputs, never scanning unrelated sources.
 * Original source contents remain owned by LSP; their existence is watched.
 * @param {object} model
 * @returns {function(): string}
 */
function watchModel (model) {
  const sources = new Set(model.originalSources || [])
  const files = [...new Set([model.configuration, ...(model.freshnessInputs || []), ...(model.artifacts || []), model.pointer].filter(Boolean))]
  const trees = [...new Set([...(model.runtimeClasses || []), ...(model.compileClasspath || []), ...(model.dependencyRoots || [])])]
  const stamp = (filename, recursive, count) => {
    if (++count.value > 100000) throw new Error('Editor dependency inventory exceeds 100000 files; narrow the declared model inputs.')
    try {
      const stat = fs.statSync(filename)
      if (sources.has(filename)) return [filename, 'source']
      const value = [filename, stat.size, stat.mtimeMs, stat.ctimeMs, stat.ino]
      if (recursive && stat.isDirectory()) {
        // Compiled class directories may be replaced in place; inspect contents.
        value.push(fs.readdirSync(filename).sort().map(name => {
          const child = path.join(filename, name)
          return fs.lstatSync(child).isSymbolicLink() ? [child, fs.readlinkSync(child), stamp(child, false, count)] : stamp(child, true, count)
        }))
      }
      return value
    } catch (error) {
      if (error.code !== 'ENOENT') throw error
      return [filename, 'missing']
    }
  }
  return () => {
    const count = { value: 0 }
    return crypto.createHash('sha256').update(JSON.stringify([
      files.map(file => stamp(file, false, count)),
      [...sources].map(file => stamp(file, false, count)),
      trees.map(file => stamp(file, true, count))
    ])).digest('hex')
  }
}

module.exports = { watchModel }
