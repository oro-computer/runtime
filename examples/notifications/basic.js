// Notifications helper utilities for the Oro Runtime demos.

import Notification, {
  NOTIFICATION_RESPONSE_EVENT,
  showNotification as showNativeNotification
} from 'oro:notification'

function defaultLog (message) {
  console.log(`[notifications] ${message}`)
}

async function ensurePermission () {
  try {
    const status = await navigator.permissions.query({ name: 'notifications' })
    if (status.state === 'granted') return true
    const req = await navigator.permissions.request({ name: 'notifications' })
    return req.state === 'granted'
  } catch {}
  if (typeof Notification?.requestPermission === 'function') {
    try {
      const res = await Notification.requestPermission()
      return res === 'granted'
    } catch {}
  }
  return false
}

export function createNotificationController ({
  log = defaultLog,
  onResponse
} = {}) {
  const handler = (event) => {
    const detail = event?.detail || {}
    log(
      `response: id=${detail.id ?? 'n/a'} action=${detail.action ?? 'default'}`
    )
    if (onResponse) onResponse(detail)
  }

  globalThis.addEventListener(NOTIFICATION_RESPONSE_EVENT, handler)

  async function showNotification (title, options = {}) {
    const ok = await ensurePermission()
    if (!ok) {
      log('permission denied')
      throw new Error('Notification permission denied')
    }
    await showNativeNotification(title, options)
    log('notification shown')
  }

  function dispose () {
    globalThis.removeEventListener(NOTIFICATION_RESPONSE_EVENT, handler)
  }

  return {
    ensurePermission,
    showNotification,
    dispose
  }
}

export async function demo () {
  const controller = createNotificationController()
  await controller.showNotification('Hello from Oro Runtime', {
    body: 'Tap to focus the app',
    tag: 'demo',
    silent: false,
    requireInteraction: false
  })
  return controller.dispose
}

export default demo
