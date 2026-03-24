import { test } from 'oro:test'
import * as application from 'oro:application'
import process from 'oro:process'
import dgram from 'oro:dgram'

// Stress dispatcher flush and WM_APP while UDP messages are flowing.
test('windows: dispatcher + udp stress', async (t) => {
  if (process.platform !== 'win32') return t.comment('skipping: windows-only')

  const base = 'Dispatcher UDP Stress'
  const winPromise = application.getCurrentWindow()

  // UDP setup
  const address = '127.0.0.1'
  const server = dgram.createSocket({ type: 'udp4', reuseAddr: true })
  const client = dgram.createSocket('udp4')

  let count = 0
  server.on('message', () => {
    count++
  })

  await new Promise((resolve, reject) => {
    server.once('error', reject)
    server.bind(0, address, resolve)
  })

  const { port } = server.address()

  // Queue a burst of title updates and interleave UDP sends
  const ops = []
  for (let i = 0; i < 25; i++) {
    ops.push(winPromise.then((w) => w.setTitle(`${base} ${i}`)))
    ops.push(
      new Promise((resolve) =>
        client.send('ping', port, address, () => resolve())
      )
    )
  }

  await Promise.all(ops)
  const win = await winPromise
  t.equal(
    win.getTitle(),
    `${base} 24`,
    'last title applied with concurrent UDP traffic'
  )
  t.ok(count >= 10, 'udp messages received during dispatch stress')

  server.close()
  client.close()
})
