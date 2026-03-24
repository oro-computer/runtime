// Desktop lifecycle mode demo
// - Logs current desktop lifecycle mode and shows how to switch it
// - Demonstrates emit-only behavior by listening for application events
//
// Usage: open this in your app (e.g., import from your index.js) and interact
// with the window (focus/blur, minimize/restore) to see logs.

import application from 'oro:application'

const log = (m) => console.log(`[lifecycle:desktop] ${m}`)

const alwaysRunning = String(
  application.config?.lifecycle_desktop_always_running ?? 'true'
)
log(`lifecycle_desktop_always_running = ${alwaysRunning}`)

globalThis.addEventListener('applicationpause', () =>
  log('applicationpause (emit-only on desktop by default)')
)
globalThis.addEventListener('applicationresume', () =>
  log('applicationresume (emit-only on desktop by default)')
)

globalThis.addEventListener('blur', () => log('DOM blur'))
globalThis.addEventListener('focus', () => log('DOM focus'))

log(
  'Tip: set lifecycle_desktop_always_running = false in oro.toml to enable real native pause/resume on desktop.'
)
log(
  'Linux note: pause uses an async path to avoid GTK deadlocks; recreate fs.watch after applicationresume.'
)
