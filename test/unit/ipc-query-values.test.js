import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

function extractFunction (path, name) {
  const source = readFileSync(new URL(path, import.meta.url), 'utf8')
  const start = source.indexOf(`export async function ${name} (`)
  const end = source.indexOf('\n/**', start)
  assert.ok(start >= 0 && end > start)
  return source.slice(start, end).replace('export ', '')
}

test('window user scripts reach IPC without an extra URI encoding layer', async () => {
  const calls = []
  const context = vm.createContext({
    __args: { index: 0 },
    formatURL: value => value,
    serializeConfig: () => '',
    ApplicationWindow: class { constructor (data) { Object.assign(this, data) } },
    ipc: {
      async request (command, options) {
        calls.push({ command, options })
        return { data: { index: 1 } }
      }
    }
  })
  const createWindow = vm.runInContext(`${extractFunction('../../api/application.js', 'createWindow')}; createWindow`, context)
  for (const userScript of ['', 'globalThis.publicValue = "public%2Fdata + %2520"\n']) {
    await createWindow({ path: '/public.html', userScript })
    const { command, options } = calls.at(-1)
    assert.equal(command, 'window.create')
    assert.equal(options.userScript, userScript)
    assert.equal(new URLSearchParams(options).get('userScript'), userScript)
  }
})

test('system and tray menu DSL reaches IPC with its original newlines and percent escapes', async () => {
  const calls = []
  const context = vm.createContext({
    os: { platform: () => 'linux' },
    ipc: {
      Result: { from: value => value },
      async request (command, options) {
        calls.push({ command, options })
        return { data: {} }
      }
    }
  })
  const setMenu = vm.runInContext(`${extractFunction('../../api/application/menu.js', 'setMenu')}; setMenu`, context)
  const value = 'File:\n  Public %2F + %2520: o;\n'
  for (const type of ['system', 'tray']) {
    const result = await setMenu({ value, index: 0 }, type)
    assert.equal(result.err, undefined)
    const call = calls.at(-1)
    assert.equal(call.command, type === 'system' ? 'application.setSystemMenu' : 'application.setTrayMenu')
    assert.equal(call.options.value, value)
    assert.equal(new URLSearchParams(call.options).get('value'), value)
  }
})
