import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'

/** Connect only to a declared public qualification package; never inspect another application's renderer. */
export async function connectPublicRenderer ({ serial, bundle, adb = 'adb', timeoutMs = 30000 }) {
  assert.match(bundle, /^oro\.runtime\.qualification\.[a-z][a-z0-9.]*$/)
  assert.ok(serial)
  const device = (...args) => execFileSync(adb, ['-s', serial, ...args], { encoding: 'utf8', timeout: 15000 }).trim()
  const deadline = Date.now() + timeoutMs
  let port
  let socket
  async function close () {
    socket?.close()
    if (port) {
      try { device('forward', '--remove', 'tcp:' + port) } catch {}
      port = null
    }
  }
  try {
    let pid
    while (Date.now() < deadline) {
      try { pid = device('shell', 'pidof', bundle).split(/\s+/)[0] } catch {}
      if (/^\d+$/.test(pid || '')) break
      await new Promise(resolve => setTimeout(resolve, 200))
    }
    assert.match(pid || '', /^\d+$/, 'Public process unavailable')
    port = device('forward', 'tcp:0', 'localabstract:webview_devtools_remote_' + pid)
    let target
    while (Date.now() < deadline) {
      try {
        const response = await fetch('http://127.0.0.1:' + port + '/json/list', { signal: AbortSignal.timeout(2000) })
        target = (await response.json()).find(candidate => {
          try {
            const url = new URL(candidate.url)
            return candidate.type === 'page' && url.hostname.toLowerCase() === bundle && url.pathname === '/index.html'
          } catch { return false }
        })
      } catch {}
      if (target) break
      await new Promise(resolve => setTimeout(resolve, 200))
    }
    assert.ok(target, 'Public renderer unavailable')
    socket = new globalThis.WebSocket(target.webSocketDebuggerUrl)
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Public renderer socket timeout')), 5000)
      socket.addEventListener('open', () => { clearTimeout(timer); resolve() }, { once: true })
      socket.addEventListener('error', error => { clearTimeout(timer); reject(error) }, { once: true })
    })
    let sequence = 0
    const pending = new Map()
    socket.addEventListener('message', event => {
      const message = JSON.parse(String(event.data))
      const request = pending.get(message.id)
      if (!request) return
      pending.delete(message.id)
      clearTimeout(request.timer)
      if (message.error) request.reject(new Error('Public renderer request failed'))
      else request.resolve(message.result)
    })
    socket.addEventListener('close', () => {
      for (const request of pending.values()) {
        clearTimeout(request.timer)
        request.reject(new Error('Public renderer closed'))
      }
      pending.clear()
    })
    return {
      close,
      async evaluate (expression) {
        const id = ++sequence
        const result = await new Promise((resolve, reject) => {
          const timer = setTimeout(() => {
            pending.delete(id)
            reject(new Error('Public evaluation timed out'))
          }, 95000)
          pending.set(id, { resolve, reject, timer })
          socket.send(JSON.stringify({
            id,
            method: 'Runtime.evaluate',
            params: {
              expression, awaitPromise: true, returnByValue: true, userGesture: true
            }
          }))
        })
        if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails))
        return result.result.value
      }
    }
  } catch (error) {
    await close()
    throw error
  }
}
