'use strict'

const fs = require('node:fs')
const path = require('node:path')
const net = require('node:net')
const crypto = require('node:crypto')
const { setTimeout: delay } = require('node:timers/promises')
const { operationSignal } = require('./process.cjs')

/**
 * Serialize preparation across processes with a loopback OS lock. Process death
 * releases the socket; unrelated hash collisions only serialize preparations.
 * @param {string} identity Canonical project path and language family.
 * @param {Function} callback
 * @returns {Promise<*>}
 */
async function locked (identity, callback) {
  const port = 20000 + crypto.createHash('sha256').update(identity).digest().readUInt32BE(0) % 40000
  const signal = operationSignal()
  const deadline = Date.now() + 300000
  let server
  for (;;) {
    signal?.throwIfAborted()
    server = net.createServer(socket => socket.destroy())
    try {
      await new Promise((resolve, reject) => {
        server.once('error', reject)
        server.listen({ host: '127.0.0.1', port, exclusive: true }, resolve)
      })
      break
    } catch (error) {
      server.close()
      if (error.code !== 'EADDRINUSE') throw error
      if (Date.now() >= deadline) throw new Error('Timed out waiting for another editor preparation; close the other preparation process and retry.')
      await delay(100, undefined, { signal })
    }
  }
  try { signal?.throwIfAborted(); return await callback() } finally { await new Promise(resolve => server.close(resolve)) }
}

function alive (pid) {
  if (!Number.isInteger(pid) || pid <= 0) return false
  try { process.kill(pid, 0); return true } catch (error) { return error.code !== 'ESRCH' }
}

/** Mark a directory allocated by Oro, including failed preparation attempts. */
function markGeneration (model) {
  fs.mkdirSync(model.generation, { recursive: true })
  fs.writeFileSync(path.join(model.generation, '.oro-editor-generation.json'), JSON.stringify({ schema: 1, projectRoot: model.projectRoot }))
}

/** Mark and lease a generated directory; callers release it after backend exit. */
function lease (model) {
  const directory = model.generation
  markGeneration(model)
  const filename = path.join(directory, `.oro-lease-${process.pid}-${crypto.randomUUID()}.json`)
  fs.writeFileSync(filename, JSON.stringify({ pid: process.pid }))
  return () => { try { fs.unlinkSync(filename) } catch (error) { if (error.code !== 'ENOENT') throw error } }
}

/**
 * Keep eight inactive generations, protecting published and live generations.
 * Only directories explicitly marked by this implementation are eligible.
 * @param {string} cache
 * @returns {void}
 */
function retain (cache) {
  if (!fs.existsSync(cache)) return
  const protectedPaths = new Set()
  const names = fs.readdirSync(cache)
  for (const name of names.filter(name => name.endsWith('.json'))) {
    try {
      const pointer = JSON.parse(fs.readFileSync(path.join(cache, name), 'utf8'))
      if (pointer.generation) protectedPaths.add(path.resolve(pointer.generation))
      if (pointer.modelFile) protectedPaths.add(path.dirname(path.resolve(pointer.modelFile)))
    } catch {}
  }
  const generations = []
  for (const name of names) {
    const directory = path.join(cache, name)
    const stat = fs.lstatSync(directory)
    if (!stat.isDirectory() || stat.isSymbolicLink()) continue
    const marker = path.join(directory, '.oro-editor-generation.json')
    if (!fs.existsSync(marker) || fs.lstatSync(marker).isSymbolicLink()) continue
    let metadata
    try { metadata = JSON.parse(fs.readFileSync(marker, 'utf8')) } catch { continue }
    if (metadata.schema !== 1 || typeof metadata.projectRoot !== 'string') continue
    let active = false
    for (const filename of fs.readdirSync(directory).filter(name => /^\.oro-lease-\d+-[a-f0-9-]+\.json$/.test(name))) {
      try {
        const item = path.join(directory, filename)
        if (fs.lstatSync(item).isSymbolicLink()) { active = true; continue }
        if (alive(JSON.parse(fs.readFileSync(item, 'utf8')).pid)) active = true
        else fs.unlinkSync(item)
      } catch { active = true }
    }
    for (const name of fs.readdirSync(directory).filter(name => /^lsp-[a-zA-Z0-9]+$/.test(name))) {
      const session = path.join(directory, name)
      if (fs.lstatSync(session).isSymbolicLink()) continue
      try {
        const metadata = JSON.parse(fs.readFileSync(path.join(session, '.oro-session.json'), 'utf8'))
        if (!alive(metadata.pid)) fs.rmSync(session, { recursive: true })
      } catch {}
    }
    generations.push({ directory, active: active || protectedPaths.has(directory), time: fs.statSync(marker).mtimeMs })
  }
  const inactive = generations.filter(item => !item.active).sort((a, b) => b.time - a.time)
  for (const item of inactive.slice(8)) fs.rmSync(item.directory, { recursive: true })
  if (generations.filter(item => item.active).length >= 64) throw new Error('Editor cache has 64 active or published generations. Close unused editor sessions before preparing another model.')
}

module.exports = { locked, lease, retain, markGeneration }
