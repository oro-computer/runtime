/* global self */

self.addEventListener('install', (event) => {
  event.waitUntil(self.skipWaiting())
})

self.addEventListener('fetch', (event) => {
  if (new URL(event.request.url).pathname.endsWith('/preload-check/json')) {
    event.respondWith(new Response(JSON.stringify({ ok: true }), {
      headers: { 'content-type': 'application/json; charset=utf-8' }
    }))
  }
})
