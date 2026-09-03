import { test } from 'oro:test'

test('ipc: diagnostics.stream.chunks streams and aborts', async (t) => {
  const chunks = 12
  const size = 1024
  const interval = 10
  const url = `ipc://diagnostics.stream.chunks?chunks=${chunks}&chunkSize=${size}&interval=${interval}`

  const controller = new AbortController()
  const { signal } = controller

  const res = await fetch(url, { signal })
  t.equal(res.status, 200, 'HTTP OK for chunked stream')

  const reader = res.body.getReader()
  let received = 0
  const first = await reader.read()
  if (!first.done) received += first.value.byteLength

  t.ok(received > 0, 'received some streamed bytes')
  if (globalThis.__args.capabilities.streaming.chunkedIncremental) {
    // A webview may coalesce native chunks into one Web Streams read even when
    // the route itself delivers incrementally. Cancellation must remain valid
    // in both cases, so do not infer native delivery from read boundaries.
    controller.abort()
  } else {
    while (true) {
      const { done, value } = await reader.read()
      if (done) break
      received += value.byteLength
    }
    t.equal(
      received,
      chunks * size,
      'received the complete stream on a buffering webview backend'
    )
  }
})
