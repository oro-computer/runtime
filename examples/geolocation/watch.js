// Geolocation helpers used by the React dashboard and other examples.

function defaultLog (message) {
  console.log(`[geolocation] ${message}`)
}

export function createGeolocationWatcher ({
  onPosition,
  onError,
  log = defaultLog
} = {}) {
  let watchId = null

  async function ensurePermission () {
    try {
      const status = await navigator.permissions.query({ name: 'geolocation' })
      if (status.state === 'granted') return true
      const req = await navigator.permissions.request({ name: 'geolocation' })
      return req.state === 'granted'
    } catch {
      return false
    }
  }

  function start () {
    if (watchId != null) return
    try {
      watchId = navigator.geolocation.watchPosition(
        (pos) => {
          const { latitude, longitude, accuracy } = pos.coords || {}
          log(`position: ${latitude},${longitude} (±${accuracy ?? 'n/a'}m)`)
          if (onPosition) onPosition(pos)
        },
        (err) => {
          const message = err?.message || String(err)
          log(`error: ${message}`)
          if (onError) onError(message)
        },
        {
          enableHighAccuracy: true,
          maximumAge: 10000,
          timeout: 20000
        }
      )
    } catch (err) {
      const message = err?.message || String(err)
      log(`watch error: ${message}`)
      if (onError) onError(message)
    }
  }

  function stop () {
    if (watchId == null) return
    try {
      navigator.geolocation.clearWatch(watchId)
    } catch {}
    watchId = null
  }

  return {
    ensurePermission,
    start,
    stop,
    isWatching: () => watchId != null
  }
}

export async function demo () {
  const watcher = createGeolocationWatcher()
  const ok = await watcher.ensurePermission()
  if (!ok) {
    defaultLog('permission denied')
    return
  }
  watcher.start()

  const onPause = () => watcher.stop()
  const onResume = () => setTimeout(() => watcher.start(), 100)
  const onStop = () => watcher.stop()

  globalThis.addEventListener('applicationpause', onPause)
  globalThis.addEventListener('applicationresume', onResume)
  globalThis.addEventListener('applicationstop', onStop)

  return () => {
    globalThis.removeEventListener('applicationpause', onPause)
    globalThis.removeEventListener('applicationresume', onResume)
    globalThis.removeEventListener('applicationstop', onStop)
    watcher.stop()
  }
}

export default demo
