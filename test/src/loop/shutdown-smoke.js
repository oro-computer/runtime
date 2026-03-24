import { test } from 'oro:test'
import * as application from 'oro:application'
import dgram from 'oro:dgram'

function sleep (ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

// Exercise timers + windows + UDP then close everything and ensure test completes.
test('loop: shutdown smoke (timers, windows, udp)', async (t) => {
  // Timers
  const immediates = []
  const intervals = []
  const timeouts = []
  for (let i = 0; i < 10; i++) immediates.push(setImmediate(() => {}))
  for (let i = 0; i < 3; i++) intervals.push(setInterval(() => {}, 10))
  for (let i = 0; i < 3; i++) timeouts.push(setTimeout(() => {}, 20))

  // UDP (bound + ephemeral sender)
  const address = '127.0.0.1'
  const server = dgram.createSocket({ type: 'udp4', reuseAddr: true })
  await new Promise((resolve, reject) => {
    server.once('error', reject)
    server.bind(0, address, resolve)
  })
  const info = server.address()
  const client = dgram.createSocket('udp4')
  await new Promise((resolve) =>
    client.send('hello', info.port, address, () => resolve())
  )

  // Windows create/close cycles (skip if multi windows disabled)
  let win
  try {
    win = await application.createWindow({
      index: 1,
      path: 'frontend/index_no_js.html'
    })
    await win.close()
  } catch {
    // multi-window may be disabled; tolerate
  }

  // Cleanup timers and sockets
  intervals.forEach(clearInterval)
  timeouts.forEach(clearTimeout)
  try {
    server.close()
  } catch {}
  try {
    client.close()
  } catch {}

  // Small delay to let loop settle
  await sleep(100)
  t.ok(true, 'shutdown path exercised without hang')
})
