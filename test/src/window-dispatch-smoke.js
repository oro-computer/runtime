import { test } from 'oro:test'
import * as application from 'oro:application'

test('window: dispatch smoke via setTitle/getTitle', async (t) => {
  const win = await application.getCurrentWindow()
  const base = 'Oro Test Title'
  // Do a small burst of dispatches
  for (let i = 0; i < 5; i++) {
    await win.setTitle(`${base} ${i}`)
  }
  // Final state should reflect last dispatch
  t.equal(
    win.getTitle(),
    `${base} 4`,
    'title reflects last dispatched setTitle'
  )
})
