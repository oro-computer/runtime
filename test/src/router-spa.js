import test from 'oro:test'
import URL from 'oro:url'

// This test expects a temporary build where [webview]
// allow_any_route = true and default_index = /router-spa/index.html
// See test/scripts/test-desktop-spa.js

const basePath = 'router-spa'
const dirname = URL.resolve(import.meta.url, basePath)

test('router-spa fallback', async (t) => {
  // Request a route that does not exist so SPA fallback should trigger
  const response = await fetch(dirname + '/does/not/exist')
  t.ok(response.ok, 'fallback returns 200 OK')
  const body = (await response.text()).trim()
  t.ok(body.includes('/router-spa/index.html'), 'served SPA index as fallback')
})
