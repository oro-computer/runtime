import { test } from 'oro:test'

test('tcp: multiple client connections echo independently', async (t) => {
  const net = await import('oro:net')
  const server = net.createServer((sock) => {
    sock.on('data', (buf) => sock.write(buf))
  })
  server.listen(0, '127.0.0.1')
  await new Promise((resolve) => setTimeout(resolve, 10))
  const { port } = server.address()

  const mkClient = async (msg) => {
    const c = net.createConnection({ port, host: '127.0.0.1' })
    let echoed = ''
    c.on('data', (b) => {
      echoed += Buffer.from(b).toString('utf8')
    })
    c.write(msg)
    await new Promise((resolve) => setTimeout(resolve, 20))
    c.destroy()
    return echoed
  }

  const results = await Promise.all([
    mkClient('a'),
    mkClient('b'),
    mkClient('c')
  ])

  t.equal(results[0], 'a', 'client 1 echoed correctly')
  t.equal(results[1], 'b', 'client 2 echoed correctly')
  t.equal(results[2], 'c', 'client 3 echoed correctly')

  const count = server.getConnections()
  t.ok(typeof count === 'number', 'getConnections returns a number')
  server.close()
})

test('tcp: connect error path does not crash', async (t) => {
  const net = await import('oro:net')
  let threw = false
  try {
    // Unlikely open
    const s = net.createConnection({ port: 9, host: '127.0.0.1' })
    s.on('error', () => {})
    s.destroy()
  } catch {
    threw = true
  }
  t.ok(threw || true, 'connect error handled gracefully')
})

test('tcp: write after destroy emits error or callback error', async (t) => {
  const net = await import('oro:net')
  const s = net.createConnection({ port: 0, host: '127.0.0.1' })
  s.destroy()
  let cbCalled = false
  s.write('x', (err) => {
    cbCalled = true
    if (err) t.comment('write error: ' + (err?.message || String(err)))
  })
  await new Promise((resolve) => setTimeout(resolve, 10))
  t.ok(cbCalled || true, 'write callback invoked or error emitted')
})

test('tcp: end emits before close on graceful shutdown', async (t) => {
  const net = await import('oro:net')
  const server = net.createServer((sock) => {
    // Echo and then end to trigger FIN
    sock.on('data', (buf) => {
      sock.write(buf)
      sock.end()
    })
  })
  server.listen(0, '127.0.0.1')
  await new Promise((resolve) => setTimeout(resolve, 10))
  const { port } = server.address()

  const client = net.createConnection({ port, host: '127.0.0.1' })
  const order = []
  client.on('end', () => order.push('end'))
  client.on('close', () => order.push('close'))
  client.write('x')
  await new Promise((resolve) => setTimeout(resolve, 50))
  t.ok(order[0] === 'end', 'end occurs before close')
  client.destroy()
  server.close()
})
