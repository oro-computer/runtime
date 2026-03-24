import { test } from 'oro:test'
import dgram from 'oro:dgram'

test('udp4 default bind address is 0.0.0.0', async (t) => {
  if (process.env.ORO_ANDROID_CI || process.env.GITHUB_ACTIONS_CI) {
    return t.comment('skipping in CI/mobile')
  }
  const sock = dgram.createSocket('udp4')
  await new Promise((resolve, reject) =>
    sock.bind(0, (err) => (err ? reject(err) : resolve()))
  )
  const addr = sock.address()
  t.equal(addr.family, 'IPv4', 'IPv4 family')
  t.equal(addr.address, '0.0.0.0', 'bound to all interfaces')
  sock.close()
})

test('udp6 default bind address is ::', async (t) => {
  if (process.env.ORO_ANDROID_CI || process.env.GITHUB_ACTIONS_CI) {
    return t.comment('skipping in CI/mobile')
  }
  const sock = dgram.createSocket('udp6')
  await new Promise((resolve, reject) =>
    sock.bind(0, (err) => (err ? reject(err) : resolve()))
  )
  const addr = sock.address()
  t.equal(addr.family, 'IPv6', 'IPv6 family')
  t.ok(
    addr.address === '::' || addr.address === '::0',
    'bound to all IPv6 interfaces'
  )
  sock.close()
})
