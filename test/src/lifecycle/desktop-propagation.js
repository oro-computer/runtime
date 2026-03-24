import { test } from 'oro:test'
import application from 'oro:application'

function waitForEvent (target, type, timeout = 1500) {
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

// Desktop-only: verify applicationpause/resume are emitted and DOM focus/blur mirror
test.desktop(
  'lifecycle (desktop): propagate app + DOM on blur/focus',
  async (t) => {
    const win = await application.getCurrentWindow()

    // Prep listeners on global and window
    const pauseP = waitForEvent(globalThis, 'applicationpause')
    const domBlurP = waitForEvent(globalThis, 'blur')

    await win.blur()
    await Promise.all([pauseP, domBlurP])
    t.ok(true, 'emitted applicationpause and DOM blur')

    const resumeP = waitForEvent(globalThis, 'applicationresume')
    const domFocusP = waitForEvent(globalThis, 'focus')

    await win.focus()
    await Promise.all([resumeP, domFocusP])
    t.ok(true, 'emitted applicationresume and DOM focus')
  }
)

// Desktop-only: minimize/restore should also propagate + mirror DOM
test.desktop(
  'lifecycle (desktop): propagate app + DOM on minimize/restore',
  async (t) => {
    const win = await application.getCurrentWindow()

    const pauseP = waitForEvent(globalThis, 'applicationpause')
    const domBlurP = waitForEvent(globalThis, 'blur')
    await win.minimize()
    await Promise.all([pauseP, domBlurP])
    t.ok(true, 'minimize emitted applicationpause and DOM blur')

    const resumeP = waitForEvent(globalThis, 'applicationresume')
    const domFocusP = waitForEvent(globalThis, 'focus')
    await win.restore()
    await Promise.all([resumeP, domFocusP])
    t.ok(true, 'restore emitted applicationresume and DOM focus')
  }
)
