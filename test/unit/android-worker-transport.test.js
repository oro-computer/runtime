import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

const source = readFileSync(new URL('../../api/internal/init.js', import.meta.url), 'utf8')
const start = source.indexOf('    const preload = `')
const end = source.indexOf('\n    const objectURL =', start)
assert.ok(start >= 0 && end > start)
const origin = 'https://computer.oro.runtime.tests'

function createPreload (token) {
  return vm.runInNewContext(`${source.slice(start, end).replaceAll('import.meta.url', JSON.stringify(`${origin}/oro/internal/init.js`))}; preload`, {
    JSON,
    URL,
    __oroNativeBridgeToken: token,
    __args: { client: { id: 'parent' } },
    topClient: { id: 'parent' },
    runtimeURL: new URL(`${origin}/oro/`),
    id: 'worker',
    workerType: 'worker',
    url: encodeURIComponent(`${origin}/oro/shared-worker/worker.js`)
  })
}

for (const token of ['window-token', null]) {
  test(`worker bootstrap ${token ? 'authenticates HTTPS IPC before its first import' : 'preserves the native transport on other platforms'}`, async () => {
    const calls = []
    const errors = []
    class XMLHttpRequest {
      headers = new Map()
      open (method, url, async) { calls.push({ method, url: String(url), async, request: this }) }
      setRequestHeader (name, value) { this.headers.set(name, value) }
      send () {
        if (token) assert.ok(calls.at(-1).url.startsWith(origin + '/'), 'Android never sends mixed-content IPC')
        this.responseText = '{"data":{"platform":"android"}}'
      }
    }
    const uri = 'ipc://platform.primordials?seq=R1&value=a%2Bb%2520&runtime-worker-type=serviceworker&__sync__=true'
    let imports = 0
    const context = vm.createContext({
      XMLHttpRequest,
      URL,
      location: { href: `blob:${origin}/worker` },
      addEventListener () {},
      removeEventListener () {},
      reportError: error => errors.push(error),
      async importModule (specifier) {
        imports++
        if (specifier.endsWith('/internal/init.js')) {
          const request = new XMLHttpRequest()
          request.open('GET', uri, false)
          request.send()
          assert.equal(JSON.parse(request.responseText).data.platform, 'android')
        }
        return { onReady: callback => callback() }
      }
    })
    await vm.runInContext(`(async () => { ${createPreload(token).replaceAll('await import(', 'await importModule(')} })()`, context)
    assert.deepEqual(errors, [])
    assert.equal(imports, 3, 'bootstrap reaches the worker entry module')
    assert.equal(calls[0].async, false, 'metadata is available synchronously')
    assert.equal(calls[0].url, token ? `${origin}/__oro_ipc__/platform.primordials?${uri.split('?')[1]}` : uri)
    assert.equal(calls[0].request.headers.get('X-Oro-Bridge-Token'), token ?? undefined)
    assert.equal(context.__oroNativeBridgeToken, token, 'nested workers can inherit the same token')
    assert.equal(Object.keys(context).includes('__oroNativeBridgeToken'), false)

    const ordinary = new XMLHttpRequest()
    ordinary.open('GET', `${origin}/data.json`, true)
    assert.equal(calls.at(-1).url, `${origin}/data.json`)
    assert.equal(ordinary.headers.size, 0, 'ordinary resource requests do not disclose the IPC token')

    const module = new XMLHttpRequest()
    const moduleURL = 'oro://computer.oro.runtime.tests/node_modules/a/index.js?value=a%2Bb#source'
    module.open('GET', moduleURL, false)
    assert.equal(calls.at(-1).url, token ? moduleURL.replace('oro:', 'https:') : moduleURL)
    assert.equal(calls.at(-1).async, false, 'module loads remain synchronous')
    assert.equal(module.headers.size, 0, 'module loads do not disclose the IPC token')

    const foreign = new XMLHttpRequest()
    const foreignURL = 'oro://foreign.example/module.js'
    foreign.open('GET', foreignURL, false)
    assert.equal(calls.at(-1).url, foreignURL, 'only application assets use the HTTPS route')
    assert.equal(foreign.headers.size, 0)

    const write = new XMLHttpRequest()
    write.open('POST', 'ipc://serviceWorker.fetch.response.write?seq=R2', true)
    assert.equal(calls.at(-1).method, 'POST')
    assert.equal(calls.at(-1).async, true)
    assert.equal(calls.at(-1).url, token ? `${origin}/__oro_ipc__/serviceWorker.fetch.response.write?seq=R2` : 'ipc://serviceWorker.fetch.response.write?seq=R2')
  })
}
