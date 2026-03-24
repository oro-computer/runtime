// MediaDevices helpers used by the React dashboard and kitchen sink example.

function defaultLog (message) {
  console.log(`[media] ${message}`)
}

export function createMediaController ({ log = defaultLog } = {}) {
  let stream = null
  let videoElement = null

  async function ensurePermissions () {
    try {
      const mic = await navigator.permissions.query({ name: 'microphone' })
      const cam = await navigator.permissions.query({ name: 'camera' })
      if (mic.state === 'granted' && cam.state === 'granted') return true
    } catch {}
    // getUserMedia will surface prompts as needed
    return true
  }

  function setVideoElement (element) {
    videoElement = element || null
    if (videoElement) {
      videoElement.autoplay = true
      videoElement.muted = true
      videoElement.playsInline = true
      if (stream) {
        try {
          videoElement.srcObject = stream
        } catch {}
      }
    }
  }

  async function start () {
    if (stream) return stream
    try {
      stream = await navigator.mediaDevices.getUserMedia({
        audio: true,
        video: true
      })
      if (videoElement) {
        try {
          videoElement.srcObject = stream
        } catch {}
      }
      log('media started')
      return stream
    } catch (error) {
      const message = error?.message || String(error)
      log(`getUserMedia error: ${message}`)
      throw error
    }
  }

  function stop () {
    if (!stream) return
    try {
      for (const track of stream.getTracks()) track.stop()
    } catch {}
    stream = null
    if (videoElement) {
      try {
        videoElement.srcObject = null
      } catch {}
    }
    log('media stopped')
  }

  function dispose () {
    stop()
    videoElement = null
  }

  return {
    ensurePermissions,
    start,
    stop,
    dispose,
    setVideoElement,
    getStream: () => stream
  }
}

export async function demo () {
  const controller = createMediaController()
  const ok = await controller.ensurePermissions()
  if (!ok) {
    defaultLog('permissions not granted')
    return () => controller.dispose()
  }

  let overlay = document.getElementById('media-demo-overlay')
  if (!overlay) {
    overlay = document.createElement('video')
    overlay.id = 'media-demo-overlay'
    overlay.style.width = '240px'
    overlay.style.height = '180px'
    overlay.style.border = '1px solid #ddd'
    overlay.style.position = 'fixed'
    overlay.style.right = '12px'
    overlay.style.bottom = '12px'
    overlay.style.borderRadius = '12px'
    overlay.style.boxShadow = '0 18px 40px rgba(15, 23, 42, 0.32)'
    document.body.appendChild(overlay)
  }
  controller.setVideoElement(overlay)
  await controller.start()

  const onPause = () => controller.stop()
  const onResume = () =>
    setTimeout(() => controller.start().catch(() => {}), 150)
  const onStop = () => controller.stop()

  globalThis.addEventListener('applicationpause', onPause)
  globalThis.addEventListener('applicationresume', onResume)
  globalThis.addEventListener('applicationstop', onStop)

  return () => {
    globalThis.removeEventListener('applicationpause', onPause)
    globalThis.removeEventListener('applicationresume', onResume)
    globalThis.removeEventListener('applicationstop', onStop)
    controller.dispose()
    if (overlay && overlay.parentNode) overlay.parentNode.removeChild(overlay)
  }
}

export default demo
