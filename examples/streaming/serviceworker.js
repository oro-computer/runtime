/* global self */

self.addEventListener('install', () => {
  self.skipWaiting()
})

self.addEventListener('activate', (event) => {
  event.waitUntil(self.clients.claim())
})

self.addEventListener('fetch', (event) => {
  const url = new URL(event.request.url)
  if (url.pathname.endsWith('/stream')) {
    event.respondWith(streamingResponse())
  }
})

function streamingResponse () {
  const encoder = new TextEncoder()
  const start = Date.now()
  const stream = new ReadableStream({
    start (controller) {
      let i = 0
      const id = setInterval(() => {
        i++
        const elapsed = Math.floor((Date.now() - start) / 1000)
        controller.enqueue(encoder.encode(`chunk ${i} @ ${elapsed}s\n`))
        if (i >= 10) {
          clearInterval(id)
          controller.close()
        }
      }, 250)
    }
  })
  const headers = new Headers({
    'content-type': 'text/plain; charset=utf-8',
    // Hint the native layer to use streaming where supported
    'transfer-encoding': 'chunked'
  })
  return new Response(stream, { status: 200, headers })
}
