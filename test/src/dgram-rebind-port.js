import { test } from 'oro:test'
import dgram from 'oro:dgram'

function sleep (ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

test('udp: bound socket preserves port across pause/resume', async (t) => {
  const server = dgram.createSocket({ type: 'udp4', reuseAddr: true })
  await new Promise((resolve, reject) => {
    server.once('error', reject)
    server.bind(0, '127.0.0.1', resolve)
  })

  const before = server.address().port
  // Simulate app pause/resume
  globalThis.dispatchEvent(new Event('applicationpause'))
  await sleep(150)
  globalThis.dispatchEvent(new Event('applicationresume'))
  await sleep(400)

  const after = server.address().port
  t.equal(after, before, 'port preserved across resume')

  server.close()
})
