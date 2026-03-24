import { test } from 'oro:test'
import dgram from 'oro:dgram'

function sleep (ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

test('udp: quick pause/resume does not break receives', async (t) => {
  const address = '127.0.0.1'
  const port = 45210

  const server = dgram.createSocket({ type: 'udp4', reuseAddr: true })
  const client = dgram.createSocket('udp4')

  await new Promise((resolve, reject) => {
    server.once('error', reject)
    server.bind(port, address, resolve)
  })

  // Quick pause/resume cycle
  globalThis.dispatchEvent(new Event('applicationpause'))
  await sleep(25)
  globalThis.dispatchEvent(new Event('applicationresume'))

  // Allow services to reattach
  await sleep(250)

  const got = new Promise((resolve) =>
    server.once('message', () => resolve(true))
  )
  await new Promise((resolve) => client.send('ok', port, address, resolve))

  const ok = await Promise.race([
    got,
    new Promise((_resolve, reject) =>
      setTimeout(() => reject(new Error('timeout')), 1500)
    )
  ])

  t.equal(ok, true, 'received udp packet after quick pause/resume')
  server.close()
  client.close()
})
