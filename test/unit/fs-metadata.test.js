import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

const source = readFileSync(new URL('../../api/fs/promises.js', import.meta.url), 'utf8')

for (const operation of ['stat', 'lstat']) {
  test(`fs.promises.${operation} reads metadata without opening the target`, async () => {
    const start = source.indexOf(`export async function ${operation} (`)
    const end = source.indexOf('\n/**', start)
    const metadata = { st_mode: operation === 'stat' ? 0o100000 : 0o120777, st_size: 12 }
    let error = null
    const calls = []
    const context = vm.createContext({
      normalizePath: path => path,
      async visit () { throw Object.assign(new Error('permission denied'), { code: 'EACCES' }) },
      ipc: {
        async request (command, params, options) {
          calls.push({ command, params, options })
          return error ? { err: error } : { data: metadata }
        }
      },
      Stats: { from: (data, bigint) => ({ data, bigint }) }
    })
    const lookup = vm.runInContext(`${source.slice(start, end).replace('export ', '')}; ${operation}`, context)
    const result = await lookup('/app/unreadable', { bigint: true })
    assert.equal(result.data, metadata)
    assert.equal(result.bigint, true)
    assert.equal(calls[0].command, `fs.${operation}`)
    assert.equal(calls[0].params.path, '/app/unreadable')
    error = Object.assign(new Error('not found'), { code: 'ENOENT' })
    await assert.rejects(lookup('/app/missing'), value => value === error)
  })
}
