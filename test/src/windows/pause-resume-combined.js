import { test } from 'oro:test'
import dgram from 'oro:dgram'

function sleep (ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

test('windows: pause/resume stress with timers + udp', async (t) => {
  if (process.platform !== 'win32') return t.comment('skipping: windows-only')

  const address = '127.0.0.1'
  const port = 43001

  const server = dgram.createSocket({ type: 'udp4', reuseAddr: true })
  const client = dgram.createSocket('udp4')

  await new Promise((resolve, reject) => {
    server.once('error', reject)
    server.bind(port, address, resolve)
  })

  let count = 0
  server.on('message', () => {
    count++
  })

  // Burst of timers during pause/resume cycles
  const cycles = 3
  for (let i = 0; i < cycles; i++) {
    for (let j = 0; j < 25; j++) setTimeout(() => {}, 1)
    globalThis.dispatchEvent(new Event('applicationpause'))
    await sleep(150)
    globalThis.dispatchEvent(new Event('applicationresume'))
    await sleep(300)
    await new Promise((resolve) =>
      client.send('ping', port, address, () => resolve())
    )
  }

  await sleep(300)
  t.ok(count >= cycles, 'received pings across pause/resume cycles')

  server.close()
  client.close()
})
