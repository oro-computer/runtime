import { test } from 'oro:test'
import * as application from 'oro:application'
import process from 'oro:process'

// Intentionally queue dispatcher work as early as possible to stress
// pre-pump queuing and notifyReady() flushing on Windows.
test('windows: early dispatcher queue/flush smoke', async (t) => {
  if (process.platform !== 'win32') {
    return t.comment('skipping: windows-only')
  }

  const base = 'Early Dispatch'
  const winPromise = application.getCurrentWindow()

  // Queue a burst of setTitle operations before awaiting the window.
  const ops = []
  for (let i = 0; i < 10; i++) {
    ops.push(winPromise.then((w) => w.setTitle(`${base} ${i}`)))
  }

  // Await completion and verify final state reflects last dispatch
  await Promise.all(ops)
  const win = await winPromise
  t.equal(win.getTitle(), `${base} 9`, 'last title applied after queued flush')
})
