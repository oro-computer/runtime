import { test } from 'oro:test'

test('ipc: diagnostics.stream.sse emits and cancels', async (t) => {
  const count = 8
  const interval = 15
  const url = `ipc://diagnostics.stream.sse?count=${count}&interval=${interval}&name=test`

  const received = []
  await new Promise((resolve, reject) => {
    const es = new globalThis.EventSource(url)
    const timeout = setTimeout(() => {
      try {
        es.close()
      } catch {}
      reject(new Error('SSE timeout'))
    }, 3000)

    es.addEventListener('test', (ev) => {
      received.push(ev.data)
      if (received.length === Math.ceil(count / 2)) {
        // Cancel mid-stream
        es.close()
        clearTimeout(timeout)
        resolve()
      }
    })

    es.onerror = (err) => {
      // Some platforms may emit error on close; ignore if we already received some
      if (received.length === 0) {
        clearTimeout(timeout)
        reject(err)
      }
    }
  })

  t.ok(received.length > 0, 'received some SSE events')
  t.ok(received.length <= count, 'did not exceed planned event count')
})
