import background from 'oro:background'

const TASK_ID = 'example-periodic-sync'

const logEl = document.querySelector('#log')
const registerButton = document.querySelector('#register')
const scheduleButton = document.querySelector('#schedule')
const cancelButton = document.querySelector('#cancel')

function log (message) {
  const timestamp = new Date().toISOString()
  const entry = document.createElement('div')
  entry.textContent = `${timestamp} ${message}`
  logEl.prepend(entry)
}

async function registerTask () {
  try {
    await background.register({
      id: TASK_ID,
      entry: 'examples/background-scheduler/worker.js',
      trigger: {
        type: 'interval',
        minimumInterval: 5 * 60 * 1000
      },
      keepAlive: false,
      permissions: ['network']
    })
    log('Registered background task')
  } catch (err) {
    log(`Failed to register task: ${err.message}`)
  }
}

async function scheduleTask () {
  try {
    await background.schedule(TASK_ID)
    log('Scheduled background task')
  } catch (err) {
    log(`Failed to schedule task: ${err.message}`)
  }
}

async function cancelTask () {
  try {
    await background.cancel(TASK_ID)
    log('Cancelled background task')
  } catch (err) {
    log(`Failed to cancel task: ${err.message}`)
  }
}

registerButton.addEventListener('click', registerTask)
scheduleButton.addEventListener('click', scheduleTask)
cancelButton.addEventListener('click', cancelTask)

log(
  background.available
    ? 'Background services available on this platform'
    : 'Background services unavailable on this platform'
)
