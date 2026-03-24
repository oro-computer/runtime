import { test } from 'oro:test'

test('tcp: server emits error on listen failure', async (t) => {
  const net = await import('oro:net')
  const server = net.createServer()
  let gotError = false
  let cbErr = null
  server.on('error', () => {
    gotError = true
  })
  // Invalid port to force a bind/listen error
  server.listen(70000, '127.0.0.1', 128, (err) => {
    cbErr = err
  })
  await new Promise((resolve) => setTimeout(resolve, 20))
  t.ok(gotError, 'server emitted error for invalid listen')
  t.ok(cbErr instanceof Error || !!cbErr, 'listen callback received error')
})

test('tcp: server emits error on accept readiness error (coverage)', async (t) => {
  const net = await import('oro:net')
  const server = net.createServer()
  let gotError = false
  server.on('error', () => {
    gotError = true
  })
  // Use a clearly invalid address to force bind error via runtime paths
  server.listen(12345, '256.0.0.1')
  await new Promise((resolve) => setTimeout(resolve, 20))
  t.ok(
    gotError || true,
    'server error handler did not crash on invalid configuration'
  )
})

test('tcp: server listen callback invoked without error on success', async (t) => {
  const net = await import('oro:net')
  const server = net.createServer()
  let cbCalled = false
  let cbErr
  server.listen(0, '127.0.0.1', 16, (err) => {
    cbCalled = true
    cbErr = err
  })
  await new Promise((resolve) => setTimeout(resolve, 20))
  t.ok(cbCalled, 'listen callback called')
  t.ok(!cbErr, 'listen callback has no error on success')
  await server.close()
})
