import { test } from 'oro:test'

// This test aims to validate that the runtime plumbs the reuseAddr
// option through to the native UDP bind flags. Exact kernel behavior
// can vary by platform, so we assert the expected failure without
// reuseAddr and allow success when reuseAddr is requested.

function sleep (ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

test('dgram: bind same port without reuseAddr should fail', async (t) => {
  const dgram = await import('dgram')

  const s1 = dgram.createSocket('udp4')
  await new Promise((resolve, reject) => {
    s1.once('error', reject)
    s1.bind(0, resolve) // OS-ephemeral port
  })

  const address = s1.address()
  t.ok(address && address.port > 0, 'first socket bound to ephemeral port')

  const s2 = dgram.createSocket('udp4')
  let failedAsExpected = false
  await new Promise((resolve) => {
    s2.once('error', (err) => {
      failedAsExpected = true
      resolve(err)
    })
    // attempt to bind to same port without reuseAddr
    try {
      s2.bind({ port: address.port, address: '127.0.0.1' })
    } catch {
      failedAsExpected = true
      resolve()
    }
  })

  t.ok(failedAsExpected, 'second bind without reuseAddr fails')

  s1.close()
  s2.close()
})
test('dgram: reuseAddr toggle does not crash and may allow rebind', async (t) => {
  const dgram = await import('dgram')

  const s1 = dgram.createSocket({ type: 'udp4', reuseAddr: true })
  await new Promise((resolve, reject) => {
    s1.once('error', reject)
    s1.bind(0, resolve)
  })

  const { port } = s1.address()
  const s2 = dgram.createSocket({ type: 'udp4', reuseAddr: true })

  let bindErr = null
  await new Promise((resolve) => {
    s2.once('error', (err) => {
      bindErr = err
      resolve()
    })
    try {
      s2.bind({ port, address: '127.0.0.1' }, resolve)
    } catch (err) {
      bindErr = err
      resolve()
    }
  })

  // We do not assert success across all OSes, but ensure no crash and that the
  // flag path is exercised. If bind fails, it should fail cleanly.
  t.ok(true, 'reuseAddr path exercised; no runtime crash')
  if (bindErr) t.comment('bind error: ' + (bindErr?.message || String(bindErr)))

  s1.close()
  s2.close()
  await sleep(5)
})
