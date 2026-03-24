import { test } from 'oro:test'

test('tcp: connect emits before data and queued writes flush', async (t) => {
  const net = await import('oro:net')

  const server = net.createServer((sock) => {
    sock.on('data', (b) => sock.write(b))
  })
  server.listen(0, '127.0.0.1')
  await new Promise((resolve) => setTimeout(resolve, 10))
  const { port } = server.address()

  const client = net.createConnection({ port, host: '127.0.0.1' })

  let connected = false
  client.on('connect', () => {
    connected = true
  })

  // Write before connect; should be queued and flushed after 'connect'
  client.write('hello')

  let echoed = ''
  client.on('data', (b) => {
    echoed += Buffer.from(b).toString('utf8')
  })

  await new Promise((resolve) => setTimeout(resolve, 50))

  t.ok(connected || echoed.length > 0, 'connect emitted or data received')
  t.equal(echoed, 'hello', 'queued write flushed and echoed')

  client.destroy()
  await server.close()
})

test('tcp: resolves hostnames for bind/connect', async (t) => {
  const net = await import('oro:net')
  const server = net.createServer((sock) => {
    sock.end()
  })
  server.listen(0, 'localhost')
  await new Promise((resolve) => setTimeout(resolve, 15))
  const { port } = server.address()

  const client = net.createConnection({ port, host: 'localhost' })
  await new Promise((resolve, reject) => {
    client.once('connect', resolve)
    client.once('error', reject)
    setTimeout(() => reject(new Error('connect timeout')), 200)
  })

  client.destroy()
  await server.close()
  t.pass('hostname bind/connect succeeded')
})

test('tcp: second connect while pending throws immediate error', async (t) => {
  const net = await import('oro:net')
  const server = net.createServer(() => {})
  server.listen(0, '127.0.0.1')
  await new Promise((resolve) => setTimeout(resolve, 10))
  const { port } = server.address()

  const client = net.createConnection({ port, host: '127.0.0.1' })
  t.throws(
    () => client.connect(port, '127.0.0.1'),
    /already in progress/i,
    'second connect should synchronously error'
  )

  await new Promise((resolve) => {
    client.once('connect', resolve)
    client.once('error', resolve)
  })
  client.destroy()
  await server.close()
})
test('tcp: connect event fires for basic connect', async (t) => {
  const net = await import('oro:net')
  const server = net.createServer((sock) => sock.on('data', () => {}))
  server.listen(0, '127.0.0.1')
  await new Promise((resolve) => setTimeout(resolve, 10))
  const { port } = server.address()

  const client = net.createConnection({ port, host: '127.0.0.1' })
  let fired = false
  client.on('connect', () => {
    fired = true
  })
  await new Promise((resolve) => setTimeout(resolve, 30))
  t.ok(fired, 'connect event fired')

  client.destroy()
  await server.close()
})
