import { test } from 'oro:test'

test('service worker: does not inject preload for non-HTML content', async (t) => {
  const endpoint = new URL('./preload-check/json', import.meta.url)
  const registration = await navigator.serviceWorker.register(
    new URL('./preload-fixture.js', import.meta.url),
    { scope: new URL('./preload-check/', import.meta.url).pathname, type: 'module' }
  )
  try {
    const deadline = Date.now() + 5000
    while (registration.active?.state !== 'activated') {
      if (Date.now() >= deadline) {
        const worker = registration.active || registration.waiting || registration.installing
        throw new Error(`Service worker activation timed out: ${worker?.state}`)
      }
      await new Promise((resolve) => setTimeout(resolve, 25))
    }

    const res = await fetch(endpoint, { redirect: 'manual' })
    t.equal(
      res.headers
        .get('content-type')
        ?.toLowerCase()
        .startsWith('application/json'),
      true,
      'content-type is application/json'
    )
    const body = await res.text()

    // Ensure there are no runtime preload injection markers in the JSON body
    t.equal(
      body.includes('<meta name="begin-runtime-preload">'),
      false,
      'no begin preload marker'
    )
    t.equal(
      body.includes('<meta name="end-runtime-preload">'),
      false,
      'no end preload marker'
    )
    t.deepEqual(JSON.parse(body), { ok: true }, 'receives the complete JSON response')
  } finally {
    t.equal(await registration.unregister(), true, 'unregisters the worker')
  }
})
