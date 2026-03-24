import { test } from 'oro:test'
import application from 'oro:application'

test('service worker: does not inject preload for non-HTML content', async (t) => {
  // Create a window and navigate to the SW example to register a SW
  const win = await application.createWindow({
    index: 3,
    resourcesDirectory: '.',
    path: 'frontend/index_no_js.html'
  })
  await win.navigate('examples/service-worker/index.html?auto=1')

  // Wait for the example to complete its auto flow (sets title to 'pong')
  const deadline = Date.now() + 8000
  do {
    await new Promise((resolve) => setTimeout(resolve, 50))
    if (win.getTitle() === 'pong') break
  } while (Date.now() < deadline)

  // Now issue a JSON fetch which the SW responds to with application/json
  const res = await fetch('/json', { redirect: 'manual' })
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
  t.equal(
    body.trim().startsWith('{') || body.trim().startsWith('['),
    true,
    'body looks like JSON'
  )
})
