import { test } from 'oro:test'
import dgram from 'oro:dgram'
import Buffer from 'oro:buffer'

function sleep (ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

test('udp: bound socket receives before and after pause/resume', async (t) => {
  const address = '127.0.0.1'
  const port = 41250

  const server = dgram.createSocket({ type: 'udp4', reuseAddr: true })
  const client = dgram.createSocket('udp4')

  const received = []
  server.on('message', (msg) => received.push(Buffer.from(msg).toString()))

  await new Promise((resolve, reject) => {
    server.once('error', reject)
    server.bind(port, address, resolve)
  })

  // Send first message and assert
  await new Promise((resolve) => {
    server.once('message', () => resolve())
    client.send('first', port, address)
  })
  t.equal(received.at(-1), 'first', 'received before pause')

  // Simulate application pause/resume to exercise conduit/socket restart
  globalThis.dispatchEvent(new Event('applicationpause'))
  await sleep(200)
  globalThis.dispatchEvent(new Event('applicationresume'))
  // Allow time for conduit to restart and listeners to reattach
  await sleep(500)

  await new Promise((resolve) => {
    server.once('message', () => resolve())
    client.send('second', port, address)
  })
  t.equal(received.at(-1), 'second', 'received after resume')

  server.close()
  client.close()
})
test('udp: connected client sends after pause/resume', async () => {
  const address = '127.0.0.1'
  const port = 41251

  const server = dgram.createSocket('udp4')
  const client = dgram.createSocket('udp4')

  await new Promise((resolve, reject) => {
    server.once('error', reject)
    server.bind(port, address, resolve)
  })

  await new Promise((resolve, reject) => {
    client.connect(port, address, (err) => (err ? reject(err) : resolve()))
  })

  // First send
  await new Promise((resolve) => {
    server.once('message', () => resolve())
    client.send('before')
  })

  globalThis.dispatchEvent(new Event('applicationpause'))
  await sleep(200)
  globalThis.dispatchEvent(new Event('applicationresume'))
  await sleep(500)

  // After resume send
  await new Promise((resolve) => {
    server.once('message', () => resolve())
    client.send('after')
  })

  server.close()
  client.close()
})
