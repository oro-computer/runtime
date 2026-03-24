import { test } from 'oro:test'
import application from 'oro:application'

function waitForEvent (target, type, timeout = 2000) {
  return new Promise((resolve, reject) => {
    const on = () => {
      cleanup()
      resolve(true)
    }
    const to = setTimeout(() => {
      cleanup()
      reject(new Error(`timeout waiting for ${type}`))
    }, timeout)
    const cleanup = () => {
      clearTimeout(to)
      target.removeEventListener(type, on)
    }
    target.addEventListener(type, on)
  })
}

// Cross-platform: verify DOM blur/focus always mirror window actions.
// Applicationpause/resume are platform-specific; desktop emits without pausing.
test.desktop(
  'lifecycle (desktop): DOM mirrors blur/focus actions',
  async (t) => {
    const win = await application.getCurrentWindow()

    const domBlurP = waitForEvent(globalThis, 'blur')
    await win.blur()
    await domBlurP
    t.ok(true, 'DOM blur fired')

    const domFocusP = waitForEvent(globalThis, 'focus')
    await win.focus()
    await domFocusP
    t.ok(true, 'DOM focus fired')
  }
)
