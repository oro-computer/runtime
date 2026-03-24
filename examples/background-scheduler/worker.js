// This file will execute inside the background context once the runtime
// orchestration is wired up. Until then it simply posts a message so that the
// UI can display an event when the worker bootstraps.

const workerScope = globalThis.self ?? globalThis

workerScope.addEventListener('run', () => {
  workerScope.postMessage({
    type: 'log',
    message: 'Background task run event received'
  })
})

workerScope.addEventListener('cancel', () => {
  workerScope.postMessage({ type: 'log', message: 'Background task cancelled' })
})

workerScope.addEventListener('message', (event) => {
  workerScope.postMessage({
    type: 'log',
    message: `Foreground message: ${JSON.stringify(event.data)}`
  })
})
