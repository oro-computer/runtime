import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import vm from 'node:vm'

const source = readFileSync(new URL('../../api/internal/init.js', import.meta.url), 'utf8')
const workerClass = source.slice(source.indexOf('class RuntimeWorker '), source.indexOf('\ntry {\n  RuntimeWorker.pool'))

for (const scheme of ['https', 'oro']) {
  for (const nested of [false, true]) {
    test(`${nested ? 'nested' : 'document'} workers load runtime modules and relative entries using ${scheme}`, async () => {
      const origin = `${scheme}://app.example`
      const blobs = new Map()
      const events = new EventTarget()
      class WorkerURL extends URL {
        static createObjectURL (blob) {
          const url = `blob:${origin}/${blobs.size}`
          blobs.set(url, blob)
          return url
        }
      }
      const servedURL = `${origin}/oro/internal/init.js`
      const entryURL = `${origin}/app/${nested ? 'parent.js' : 'index.html'}`
      const context = vm.createContext({
        URL: WorkerURL,
        Blob,
        EventTarget,
        GlobalWorker: class extends EventTarget {
          postMessage () {}
        },
        location: {
          href: 'oro://app.example/app/index.html',
          origin: 'oro://app.example',
          url: new URL(entryURL)
        },
        __args: { client: { id: 'parent', type: nested ? 'worker' : 'window' } },
        rand64: () => 123,
        queueMicrotask,
        addEventListener: events.addEventListener.bind(events),
        removeEventListener: events.removeEventListener.bind(events)
      })
      const Worker = vm.runInContext(`${workerClass.replaceAll('import.meta.url', JSON.stringify(servedURL))}
        RuntimeWorker.pool = new Map()
        RuntimeWorker
      `, context)
      const worker = new Worker('./child.js?mode=shared')
      const preload = await blobs.get(worker.objectURL).text()
      const imports = [...preload.matchAll(/await import\('([^']+)'\)/g)].map(match => match[1])
      assert.equal(imports.length, 3)
      assert.equal(imports[0], servedURL)
      assert.equal(imports[1], `${origin}/oro/hooks.js`)
      const workerModule = new URL(imports[2])
      assert.equal(`${workerModule.protocol}//${workerModule.host}${workerModule.pathname}`, `${origin}/oro/internal/worker.js`)
      assert.equal(workerModule.searchParams.get('source'), `${origin}/app/child.js?mode=shared`)
      assert.ok(preload.includes(`decodeURIComponent('${encodeURIComponent(`${origin}/app/child.js?mode=shared`)}')`))
    })
  }
}
