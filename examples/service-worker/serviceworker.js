/* global self */

// Minimal SW: respond to GET /ping with a small body; otherwise 404
self.addEventListener('install', () => {
  self.skipWaiting()
})

self.addEventListener('activate', (event) => {
  event.waitUntil(self.clients.claim())
})

self.addEventListener('fetch', (event) => {
  const url = new URL(event.request.url)
  console.log('fetch', url.href)
  if (url.pathname.endsWith('/ping')) {
    event.respondWith(
      new Response('pong', {
        headers: { 'content-type': 'text/plain; charset=utf-8' }
      })
    )
    return
  }

  if (url.pathname.endsWith('/json')) {
    const body = JSON.stringify({ ok: true, ts: Date.now() })
    event.respondWith(
      new Response(body, {
        headers: { 'content-type': 'application/json; charset=utf-8' }
      })
    )
    return
  }

  // Explicit 404 body to exercise one-shot response path
  event.respondWith(
    new Response('not found', {
      status: 404,
      headers: { 'content-type': 'text/plain; charset=utf-8' }
    })
  )
})
