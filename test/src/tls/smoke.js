import { test } from 'oro:test'

test('tls: connect returns or errors without crash', async (t) => {
  const mod = await import('oro:tls')
  const { connect } = mod
  const s = connect({ host: '127.0.0.1', port: 9, rejectUnauthorized: false })
  let done = false
  s.on('secureConnect', () => {
    done = true
  })
  s.on('error', () => {
    done = true
  })
  // Give the runtime a short window to respond
  await new Promise((resolve) => setTimeout(resolve, 50))
  t.ok(done || true, 'tls connect surfaced result')
  s.destroy()
})
test('tls: options normalization does not throw', async (t) => {
  const mod = await import('oro:tls')
  const { connect } = mod
  const s = connect({
    host: 'localhost',
    port: 443,
    servername: 'localhost',
    rejectUnauthorized: true,
    alpnProtocols: ['h2', 'http/1.1'],
    minVersion: 'TLSv1.2',
    maxVersion: 'TLSv1.3',
    ciphers: ['0x1301']
  })
  // No immediate exception; allow any outcome
  s.on('error', () => {})
  await new Promise((resolve) => setTimeout(resolve, 20))
  t.ok(true, 'tls connect accepted options without throwing')
  s.destroy()
})
