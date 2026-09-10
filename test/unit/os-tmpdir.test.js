import test from 'node:test'
import assert from 'node:assert/strict'
import { copyFileSync, mkdirSync, mkdtempSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { pathToFileURL } from 'node:url'

const container = '/app/data/tmp/'

for (const [platform, env, expected] of [
  ['ios', {}, '/app/data/tmp'],
  ['ios', { TMPDIR: '/custom/tmp/' }, '/custom/tmp'],
  ['android', {}, '/app/data/tmp'],
  ['android', { TMPDIR: '/custom/tmp/' }, '/custom/tmp'],
  ['linux', {}, '/tmp']
]) {
  test(`os.tmpdir uses ${expected} on ${platform}`, async () => {
    const directory = mkdtempSync(path.join(tmpdir(), 'oro-os-tmpdir-'))
    mkdirSync(path.join(directory, 'path'))
    mkdirSync(path.join(directory, 'os'))
    writeFileSync(path.join(directory, 'package.json'), '{"type":"module"}')
    copyFileSync(new URL('../../api/os.js', import.meta.url), path.join(directory, 'os.js'))
    copyFileSync(new URL('../../api/path/well-known.js', import.meta.url), path.join(directory, 'path', 'well-known.js'))
    writeFileSync(path.join(directory, 'ipc.js'), `
      export const primordials = { platform: ${JSON.stringify(platform)}, cwd: '/app/bundle/ui' }
      export default { sendSync () { return { data: { tmp: ${JSON.stringify(container)} } } } }
    `)
    writeFileSync(path.join(directory, 'util.js'), 'export const toProperCase = value => value\n')
    writeFileSync(path.join(directory, 'os', 'constants.js'), 'export default {}\n')
    const previousArgs = Object.getOwnPropertyDescriptor(globalThis, '__args')
    try {
      globalThis.__args = { env }
      const os = await import(pathToFileURL(path.join(directory, 'os.js')).href)
      assert.equal(os.tmpdir(), expected)
      assert.ok(!os.tmpdir().startsWith('/app/bundle/'))
    } finally {
      if (previousArgs) Object.defineProperty(globalThis, '__args', previousArgs)
      else delete globalThis.__args
    }
  })
}
