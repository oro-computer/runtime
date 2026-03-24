import { test } from 'oro:test'
import dgram from 'oro:dgram'

test('udp6: bind/connect/send loopback ::1', async (t) => {
  // Skip in CI due to potential lack of IPv6 loopback or firewall
  if (process.env.ORO_ANDROID_CI || process.env.GITHUB_ACTIONS_CI) {
    return t.comment('skipping in CI/mobile')
  }

  const address = '::1'
  const port = 43111
  const server = dgram.createSocket('udp6')
  const client = dgram.createSocket('udp6')

  await new Promise((resolve, reject) => {
    server.once('error', reject)
    server.bind(port, address, resolve)
  })

  const got = new Promise((resolve, reject) => {
    const to = setTimeout(() => reject(new Error('timeout ipv6 recv')), 1000)
    server.once('message', (msg) => {
      clearTimeout(to)
      resolve(String(msg))
    })
  })

  await new Promise((resolve, reject) => {
    client.connect(port, address, (err) => (err ? reject(err) : resolve()))
  })

  await new Promise((resolve) => client.send('hello6', resolve))
  const msg = await got
  t.equal(msg, 'hello6', 'received over IPv6')

  server.close()
  client.close()
})
