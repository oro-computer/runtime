import { test } from 'oro:test'
import net, {
  connect,
  createConnection,
  createServer,
  Server,
  Socket,
  TCPServer,
  TCPSocket
} from 'oro:net'

function withTimeout (promise, milliseconds, message) {
  let timer = null
  const timeout = new Promise((resolve, reject) => {
    timer = setTimeout(() => reject(new Error(message)), milliseconds)
  })

  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer))
}

test('net exports', (t) => {
  t.equal(Server, TCPServer, 'Server aliases TCPServer')
  t.equal(Socket, TCPSocket, 'Socket aliases TCPSocket')
  t.equal(net.Server, Server, 'default export includes Server')
  t.equal(net.Socket, Socket, 'default export includes Socket')
  t.equal(net.connect, connect, 'default export includes connect')
  t.equal(
    net.createConnection,
    createConnection,
    'default export includes createConnection'
  )
  t.equal(net.createServer, createServer, 'default export includes createServer')
  t.throws(
    () => connect(0),
    /port must be in the range 1 through 65535/,
    'connect validates the remote port before creating a socket'
  )
})

test('net TCP loopback', async (t) => {
  const server = createServer()
  let client = null

  try {
    server.on('connection', (socket) => {
      socket.once('data', (data) => {
        t.equal(data.toString(), 'ping', 'server receives client bytes')
        socket.end('pong')
      })
    })

    t.ok(server instanceof Server, 'createServer returns a Server')
    t.throws(
      () => server.listen(-1),
      /port must be in the range 0 through 65535/,
      'listen validates the local port before binding'
    )

    await withTimeout(
      new Promise((resolve, reject) => {
        server.once('error', reject)
        server.listen({ port: 0, host: '127.0.0.1', backlog: 128 }, (err) => {
          if (err) return reject(err)
          resolve()
        })
      }),
      2000,
      'TCP server did not begin listening'
    )

    const address = server.address()
    t.equal(server.listening, true, 'server reports its listening state')
    t.equal(address?.address, '127.0.0.1', 'server reports its local address')
    t.ok(address?.port > 0, 'server reports an assigned port')

    const response = withTimeout(
      new Promise((resolve, reject) => {
        client = connect(
          address.port,
          address.address,
          () => {
            t.equal(client.write('ping'), true, 'small writes avoid backpressure')
          }
        )
        t.ok(client instanceof Socket, 'connect returns a Socket')
        client.once('data', (data) => resolve(data.toString()))
        client.once('error', reject)
      }),
      2000,
      'TCP loopback response timed out'
    )

    t.equal(await response, 'pong', 'client receives server bytes')
    t.equal(client.remotePort, address.port, 'client reports the remote port')
  } finally {
    client?.destroy()
    await server.close()
    t.equal(server.listening, false, 'close clears the listening state')
  }
})
