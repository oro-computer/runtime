import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

const source = readFileSync(new URL('../../api/extension.js', import.meta.url), 'utf8')
const start = source.indexOf('export class Extension extends EventTarget')
const end = source.indexOf('\nexport async function load', start)

for (const type of ['unknown', 'shared', null]) {
  test(`Native extensions unload and apply new policies after a ${type} type probe`, async () => {
    let loaded = false
    let allowed = false
    let unloads = 0
    const context = vm.createContext({
      Event,
      EventTarget,
      ipc: {
        createBinding: () => ({}),
        async request (command, options) {
          if (command === 'extension.type') return { data: { type } }
          if (command === 'extension.stats') return { data: { abi: 1 } }
          if (command === 'extension.load') {
            assert.equal(loaded, false, 'previous native instance was unloaded')
            loaded = true
            allowed = options.allow !== 'none'
            return { data: { abi: 1 } }
          }
          assert.equal(command, 'extension.unload')
          assert.equal(loaded, true)
          loaded = false
          allowed = false
          unloads++
          return { data: {} }
        }
      }
    })
    const Extension = vm.runInContext(`
      const $loaded = Symbol('loaded'), $stats = Symbol('stats'), $type = Symbol('type');
      ${source.slice(start, end).replace('export class', 'class')}
      Extension
    `, context)
    for (const allow of [[], ['none'], ['context', 'ipc']]) {
      const extension = await Extension.load('simple-ipc-ping', { allow })
      assert.equal(extension.type, 'shared')
      assert.equal(extension.loaded, true)
      assert.equal(allowed, !allow.includes('none'))
      let unloadEvents = 0
      extension.addEventListener('unload', () => unloadEvents++)
      assert.equal(await extension.unload(), true)
      assert.equal(extension.loaded, false)
      assert.equal(loaded, false)
      assert.equal(unloadEvents, 1)
      await assert.rejects(extension.unload(), /Extension is not loaded/)
    }
    assert.equal(unloads, 3)
  })
}
