import { test } from 'oro:test'

test('ipc: diagnostics.stream.sse emits and cancels', async (t) => {
  const count = 8
  const interval = 15
  const url = `ipc://diagnostics.stream.sse?count=${count}&interval=${interval}&name=test`

  const received = []
  await new Promise((resolve, reject) => {
    // Runtime XHR attaches the Android bridge token. Native EventSource cannot
    // supply that header and is rejected before the IPC route is reached.
    const request = new globalThis.XMLHttpRequest()
    let offset = 0
    let settled = false
    const timeout = setTimeout(() => {
      fail('SSE timeout')
    }, 3000)

    function finish (error) {
      if (settled) return
      settled = true
      clearTimeout(timeout)
      // Aborting can synchronously dispatch a final progress event with status 0.
      request.onprogress = null
      request.onload = null
      request.onerror = null
      request.abort()
      if (error) reject(error)
      else resolve()
    }

    function fail (message) {
      finish(new Error(message))
    }

    function readEvents () {
      if (settled) return
      if (request.status !== 200) {
        fail(`SSE request failed with HTTP ${request.status}`)
        return
      }

      // Only consume complete records; progress callbacks may split an event
      // or deliver several events together on a buffering webview backend.
      const text = request.responseText
      let end = text.indexOf('\n\n', offset)
      while (end !== -1) {
        const lines = text.slice(offset, end).split('\n')
        offset = end + 2
        if (lines.includes('event: test')) {
          const data = lines.find(line => line.startsWith('data: '))
          if (data) received.push(data.slice(6))
        }
        if (received.length === Math.ceil(count / 2)) {
          finish()
          return
        }
        end = text.indexOf('\n\n', offset)
      }
    }

    request.onprogress = readEvents
    request.onload = () => {
      readEvents()
      if (received.length < Math.ceil(count / 2)) {
        fail('SSE stream ended before the expected events arrived')
      }
    }
    request.onerror = () => fail('SSE network request failed')
    request.open('GET', url, true)
    request.responseType = 'text'
    request.setRequestHeader('Accept', 'text/event-stream')
    request.send()
  })

  t.deepEqual(received, ['0', '1', '2', '3'], 'received ordered SSE events before aborting')
})
