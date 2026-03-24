import { test } from 'oro:test'

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

test('tcp: server timeout via options triggers for idle sockets', async (t) => {
  const net = await import('oro:net')

  const server = net.createServer({ timeout: 25 }, () => {
    // Intentionally idle: no reads/writes from server side
  })

  server.listen(0, '127.0.0.1')
  await sleep(10)
  const { port } = server.address()

  const client = net.createConnection({ port, host: '127.0.0.1' })
  let gotTimeout = false
  server.once('timeout', (socket) => {
    gotTimeout = true
    try {
      socket.destroy()
    } catch {}
  })

  await sleep(80)
  t.ok(gotTimeout, 'server emitted timeout for idle accepted socket (options)')

  try {
    client.destroy()
  } catch {}
  await server.close()
})

test('tcp: server.setTimeout applies to accepted sockets and invokes handler', async (t) => {
  const net = await import('oro:net')

  const server = net.createServer(() => {
    // Idle
  })

  server.listen(0, '127.0.0.1')
  server.setTimeout(30, (socket) => {
    try {
      socket.destroy()
    } catch {}
  })
  await sleep(10)
  const { port } = server.address()

  const client = net.createConnection({ port, host: '127.0.0.1' })
  let gotTimeout = false
  server.once('timeout', () => {
    gotTimeout = true
  })

  await sleep(90)
  t.ok(
    gotTimeout,
    'server emitted timeout for idle accepted socket (setTimeout)'
  )

  try {
    client.destroy()
  } catch {}
  await server.close()
})

test('tcp: server timeout NOT emitted while data continues to flow', async (t) => {
  const net = await import('oro:net')

  const server = net.createServer((sock) => {
    // Keep the socket alive and consume data
    sock.on('data', () => {})
  })

  server.listen(0, '127.0.0.1')
  server.setTimeout(30) // 30ms idle timeout, but we will keep data flowing
  await sleep(10)
  const { port } = server.address()

  const client = net.createConnection({ port, host: '127.0.0.1' })
  let gotTimeout = false
  server.on('timeout', () => {
    gotTimeout = true
  })

  // Continuously send small chunks to keep connection non-idle
  const timer = setInterval(() => {
    try {
      client.write('x')
    } catch {}
  }, 10)

  await sleep(120)
  clearInterval(timer)

  t.ok(!gotTimeout, 'server did not emit timeout while data was flowing')

  try {
    client.destroy()
  } catch {}
  await server.close()
})
