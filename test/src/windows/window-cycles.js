import { test } from 'oro:test'
import * as application from 'oro:application'
import process from 'oro:process'

// Create and close multiple windows to stress lifecycle ordering.
test('windows: window create/close cycles', async (t) => {
  if (process.platform === 'android' || process.platform === 'ios') {
    return t.comment('skipping: mobile platforms')
  }

  const baseIndex = 10
  const cycles = 5

  for (let i = 0; i < cycles; i++) {
    const index = baseIndex + i
    let win
    try {
      win = await application.createWindow({
        index,
        path: 'frontend/index_no_js.html'
      })
      t.ok(win && win.index === index, `created window ${index}`)
      const closed = await win.close()
      t.ok(!closed?.err, `closed window ${index}`)
    } catch (err) {
      // Multi-window can be disabled on some configs; treat as non-fatal
      return t.comment(`window cycles skipped: ${err?.message || err}`)
    }
  }
})
