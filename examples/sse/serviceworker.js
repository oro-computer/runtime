/* global self */

self.addEventListener('install', () => {
  self.skipWaiting()
})

self.addEventListener('activate', (event) => {
  event.waitUntil(self.clients.claim())
})

self.addEventListener('fetch', (event) => {
  const url = new URL(event.request.url)
  if (url.pathname.endsWith('/events')) {
    event.respondWith(sseResponse())
  }
})

function sseResponse () {
  const encoder = new TextEncoder()
  const stream = new ReadableStream({
    start (controller) {
      let i = 0
      const id = setInterval(() => {
        i++
        const line = `data: event ${i}\n\n`
        controller.enqueue(encoder.encode(line))
        if (i >= 10) {
          clearInterval(id)
          controller.close()
        }
      }, 200)
    }
  })
  const headers = new Headers({
    'content-type': 'text/event-stream; charset=utf-8',
    'cache-control': 'no-cache',
    connection: 'keep-alive'
  })
  return new Response(stream, { status: 200, headers })
}
