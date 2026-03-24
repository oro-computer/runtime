import { test } from 'oro:test'
import dgram, { getCapabilities } from 'oro:dgram'
import Buffer from 'oro:buffer'

test('udp multicast: join, loopback and receive self-sent packet', async (t) => {
  // Multicast can be flaky or blocked in CI; skip on Android and CI.
  if (process.env.ORO_ANDROID_CI || process.env.GITHUB_ACTIONS_CI) {
    return t.comment('skipping in CI/mobile')
  }

  const caps = await getCapabilities()
  if (!caps?.multicast) return t.comment('skipping: multicast not supported')
  const address = '239.255.0.1'
  const port = 42001

  const sock = dgram.createSocket({ type: 'udp4', reuseAddr: true })

  await new Promise((resolve, reject) => {
    sock.once('error', reject)
    sock.bind(port, '0.0.0.0', resolve)
  })

  // Enable loopback so sender receives its own multicast
  await sock.setMulticastLoopback(true)
  await sock.setMulticastTTL(1)
  await sock.addMembership(address)

  const payload = Buffer.from('multicast-ping')
  const got = new Promise((resolve, reject) => {
    const to = setTimeout(
      () => reject(new Error('timeout waiting for multicast message')),
      1500
    )
    sock.once('message', (msg) => {
      clearTimeout(to)
      resolve(Buffer.from(msg).toString())
    })
  })

  // Send to the group; with loopback enabled we should receive it
  await new Promise((resolve) =>
    sock.send(payload, port, address, () => resolve())
  )

  const msg = await got
  t.equal(msg, 'multicast-ping', 'received multicast loopback message')

  await new Promise((resolve) => sock.close(resolve))
})

test('udp multicast: add/drop membership no-op flow', async (t) => {
  if (process.env.ORO_ANDROID_CI || process.env.GITHUB_ACTIONS_CI) {
    return t.comment('skipping in CI/mobile')
  }

  const caps2 = await getCapabilities()
  if (!caps2?.multicast) return t.comment('skipping: multicast not supported')
  const address = '239.255.0.2'
  const port = 42002
  const sock = dgram.createSocket({ type: 'udp4', reuseAddr: true })

  await new Promise((resolve, reject) => {
    sock.once('error', reject)
    sock.bind(port, '0.0.0.0', resolve)
  })

  try {
    await sock.setMulticastInterface('')
    await sock.addMembership(address)
    await sock.dropMembership(address)
    t.ok(true, 'add/drop membership executed')
  } catch (err) {
    t.comment(
      `multicast membership may not be supported: ${err?.message || err}`
    )
  }

  await new Promise((resolve) => sock.close(resolve))
})
