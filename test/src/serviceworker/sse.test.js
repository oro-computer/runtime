import { test } from 'oro:test'
import application from 'oro:application'

test('service worker: SSE stream resolves', async (t) => {
  const win = await application.createWindow({
    index: 3,
    resourcesDirectory: '.',
    path: 'frontend/index_no_js.html'
  })
  await win.navigate('sse/index.html?auto=1')

  const deadline = Date.now() + 12000
  let title = ''
  do {
    await new Promise((resolve) => setTimeout(resolve, 100))
    title = win.getTitle()
    if (title === 'sse-ok') break
  } while (Date.now() < deadline)

  t.equal(title, 'sse-ok', 'SSE completed successfully')
})
