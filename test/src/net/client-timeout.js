import { test } from 'oro:test'

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

test('tcp: client setTimeout triggers on idle', async (t) => {
  const net = await import('oro:net')

  // Server accepts but does not send data; client will idle
  const server = net.createServer((sock) => {
    sock.on('data', () => {})
  })
  server.listen(0, '127.0.0.1')
  await sleep(10)
  const { port } = server.address()

  const client = net.createConnection({ port, host: '127.0.0.1' })
  let fired = false
  client.setTimeout(25, () => {
    fired = true
  })

  await sleep(90)
  t.ok(fired, 'client timeout fired on idle')

  try {
    client.destroy()
  } catch {}
  await server.close()
})
test('tcp: client setTimeout does not fire while sending/receiving', async (t) => {
  const net = await import('oro:net')

  // Echo server to generate regular read/write activity
  const server = net.createServer((sock) => {
    sock.on('data', (buf) => {
      try {
        sock.write(buf)
      } catch {}
    })
  })
  server.listen(0, '127.0.0.1')
  await sleep(10)
  const { port } = server.address()

  const client = net.createConnection({ port, host: '127.0.0.1' })
  client.setTimeout(30)

  let gotTimeout = false
  client.on('timeout', () => {
    gotTimeout = true
  })

  // Send frequent data to keep the connection active
  const interval = setInterval(() => {
    try {
      client.write('ping')
    } catch {}
  }, 10)
  await sleep(120)
  clearInterval(interval)

  t.ok(!gotTimeout, 'client did not timeout while activity was present')

  try {
    client.destroy()
  } catch {}
  await server.close()
})
