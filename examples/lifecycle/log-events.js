// Logs application lifecycle events. Useful for quick manual smoke tests.

function log (msg) {
  console.log(`[lifecycle] ${msg}`)
}

globalThis.addEventListener('applicationpause', () => log('pause'))
globalThis.addEventListener('applicationresume', () => log('resume'))
globalThis.addEventListener('applicationstop', () => log('stop'))

// Optional: simulate on desktop to see ordering in dev
if (
  globalThis.location?.protocol === 'https:' ||
  globalThis.location?.protocol === 'oro:'
) {
  setTimeout(() => {
    log('simulating pause')
    globalThis.dispatchEvent(new Event('applicationpause'))
  }, 500)

  setTimeout(() => {
    log('simulating resume')
    globalThis.dispatchEvent(new Event('applicationresume'))
  }, 1000)
}

// Keep module alive; in a real app this file can just be imported.
export {}
