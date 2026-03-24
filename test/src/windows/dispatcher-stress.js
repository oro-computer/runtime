import { test } from 'oro:test'
import * as application from 'oro:application'
import process from 'oro:process'

// Stress the Windows dispatcher by queuing many operations prior to pump.
// Ensures notifyReady() flushes queued work and WM_APP posts are handled.
test('windows: dispatcher stress with burst title updates', async (t) => {
  if (process.platform !== 'win32') return t.comment('skipping: windows-only')

  const base = 'Dispatcher Stress'
  const winPromise = application.getCurrentWindow()

  const ops = []
  // Queue a larger burst of setTitle operations before awaiting the window
  for (let i = 0; i < 50; i++) {
    ops.push(winPromise.then((w) => w.setTitle(`${base} ${i}`)))
  }

  // Interleave with timers to add more scheduling pressure
  for (let i = 0; i < 25; i++) setTimeout(() => {}, 0)
  for (let i = 0; i < 25; i++) setImmediate(() => {})

  await Promise.all(ops)
  const win = await winPromise
  t.equal(
    win.getTitle(),
    `${base} 49`,
    'last title applied after heavy queued dispatch'
  )
})
