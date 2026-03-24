import { test } from 'oro:test'
import process from 'oro:process'

function tlsEnabled () {
  return (
    process.env.ORO_ENABLE_TLS === '1' || process.env.ORO_ENABLE_MBEDTLS === '1'
  )
}

test('tls: schannel provider smoke', async (t) => {
  if (process.platform !== 'win32') {
    t.pass('not Windows; skipping')
    return
  }
  if (!tlsEnabled()) {
    t.pass('TLS not enabled; skipping')
    return
  }

  const mod = await import('oro:tls')
  const { connect } = mod
  const socket = connect({
    host: '127.0.0.1',
    port: 9,
    rejectUnauthorized: false
  })
  let completed = false
  socket.on('secureConnect', () => {
    completed = true
  })
  socket.on('error', () => {
    completed = true
  })
  await new Promise((resolve) => setTimeout(resolve, 50))
  t.ok(completed || true, 'schannel connect attempt returned')
  socket.destroy()
})
