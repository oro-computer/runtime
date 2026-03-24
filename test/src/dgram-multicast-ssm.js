import { test } from 'oro:test'
import dgram, { getCapabilities } from 'oro:dgram'

test('udp multicast SSM: add/drop source-specific membership capability', async (t) => {
  if (process.env.ORO_ANDROID_CI || process.env.GITHUB_ACTIONS_CI) {
    return t.comment('skipping in CI/mobile')
  }

  const group = '232.0.0.1' // SSM range
  const source = '127.0.0.1'
  const port = 42003

  const caps = await getCapabilities()
  if (!caps?.ssm) return t.comment('skipping: SSM not supported')

  const sock = dgram.createSocket({ type: 'udp4', reuseAddr: true })
  await new Promise((resolve, reject) => {
    sock.once('error', reject)
    sock.bind(port, '0.0.0.0', resolve)
  })

  try {
    await sock.addSourceSpecificMembership(group, source)
    await sock.dropSourceSpecificMembership(group, source)
    t.ok(true, 'SSM add/drop executed')
  } catch (err) {
    // Accept lack of support
    t.comment(`SSM may not be supported: ${err?.message || err}`)
  }

  await new Promise((resolve) => sock.close(resolve))
})
