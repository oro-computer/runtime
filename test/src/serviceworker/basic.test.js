import { test } from 'oro:test'
import application from 'oro:application'

test('service worker: simple fetch resolves', async (t) => {
  // Create a window rooted at the repo so example assets are available
  const win = await application.createWindow({
    index: 1,
    resourcesDirectory: '.',
    path: 'frontend/index_no_js.html'
  })

  // Navigate to example and run auto flow
  await win.navigate('examples/service-worker/index.html?auto=1')

  // Poll window title for result set by example (should be 'pong')
  const deadline = Date.now() + 8000
  let title = ''
  do {
    await new Promise((resolve) => setTimeout(resolve, 50))
    title = win.getTitle()
    if (title === 'pong') break
  } while (Date.now() < deadline)

  t.equal(title, 'pong', 'service worker responded with pong')
})
