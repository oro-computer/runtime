import test from 'oro:test'
import * as iroh from 'oro:iroh'
import { Buffer } from 'oro:buffer'

async function ensureServiceAvailable (t) {
  try {
    // status will throw if the service is disabled
    await iroh.status()
    return true
  } catch (err) {
    if (
      err &&
      typeof err.message === 'string' &&
      err.message.includes('Iroh service is disabled')
    ) {
      t.comment('skipping: Iroh service is disabled')
      return false
    }
    throw err
  }
}

async function withIroh (t, fn) {
  if (!(await ensureServiceAvailable(t))) {
    return
  }

  await iroh.ensureInitialized()
  try {
    await fn()
  } finally {
    try {
      await iroh.shutdown()
    } catch {
      // ignore shutdown errors so tests still complete
    }
  }
}

test('iroh init/status roundtrip', async (t) => {
  await withIroh(t, async () => {
    const status = await iroh.status()
    t.equal(
      status.initialized,
      true,
      'service reports initialized after ensureInitialized'
    )
    t.equal(typeof status.version, 'string', 'version is a string')

    const shutdownResult = await iroh.shutdown()
    t.equal(shutdownResult, true, 'shutdown returns true')

    const after = await iroh.status()
    t.equal(after.initialized, false, 'status reflects shutdown state')
  })
})

test('iroh datagram and stream flow', async (t) => {
  await withIroh(t, async () => {
    const alpn = 'socket-test'

    const server = await iroh.Endpoint.create({ alpns: [alpn] })
    const client = await iroh.Endpoint.create({ alpns: [alpn] })

    try {
      await server.bind()
      await client.bind()

      const nodeAddr = await server.nodeAddr()
      t.ok(
        nodeAddr && typeof nodeAddr === 'string',
        'server exposes node address'
      )

      const extractNodeId = (addr) => {
        const match = addr && addr.match(/[A-Z2-7]{52}/)
        return match ? match[0] : ''
      }

      const serverNodeId = extractNodeId(nodeAddr)
      t.ok(serverNodeId.length > 0, 'derived server node id')

      const acceptPromise = server.acceptAny()
      const clientConn = await client.connect({ nodeAddr, alpn })
      const serverConn = await acceptPromise

      t.ok(clientConn && serverConn, 'connections established')
      if (serverConn.remoteAlpn) {
        const remote = Buffer.from(serverConn.remoteAlpn).toString('utf8')
        t.equal(remote, alpn, 'server saw expected ALPN')
      }

      let stopWatcher = null
      const watcherEvent = new Promise((resolve, reject) => {
        clientConn
          .watchConnectionType(serverNodeId, (err, info) => {
            if (err) {
              reject(err)
              return
            }
            if (info && info.connectionType) {
              resolve(info.connectionType.name || '')
            }
          })
          .then((stop) => {
            stopWatcher = stop
          })
          .catch(reject)
      })

      // Datagram ping/pong
      await clientConn.writeDatagram('ping', { timeoutMs: 2000 })
      const ping = await serverConn.readDatagram({ timeoutMs: 2000 })
      t.equal(
        Buffer.from(ping).toString('utf8'),
        'ping',
        'server received datagram'
      )

      await serverConn.writeDatagram('pong', { timeoutMs: 2000 })
      const pong = await clientConn.readDatagram({ timeoutMs: 2000 })
      t.equal(
        Buffer.from(pong).toString('utf8'),
        'pong',
        'client received datagram'
      )

      // Unidirectional stream
      const serverRecvPromise = serverConn.acceptUnidirectionalStream()
      const clientSend = await clientConn.openUnidirectionalStream()
      await clientSend.write('hello over uni', { timeoutMs: 2000 })
      await clientSend.finish()
      const serverRecv = await serverRecvPromise
      const uniData = await serverRecv.readToEnd(65536, 2000)
      t.equal(
        Buffer.from(uniData).toString('utf8'),
        'hello over uni',
        'server read uni stream payload'
      )

      // Bidirectional stream
      const clientPairPromise = clientConn.acceptBidirectionalStream()
      const [serverSend, serverRecvBi] =
        await serverConn.openBidirectionalStream()
      const [clientSendBi, clientRecvBi] = await clientPairPromise

      await serverSend.write('hello from server', { timeoutMs: 2000 })
      await serverSend.finish()
      const fromServer = await clientRecvBi.readToEnd(65536, 2000)
      t.equal(
        Buffer.from(fromServer).toString('utf8'),
        'hello from server',
        'client read bi stream payload'
      )

      await clientSendBi.write('hi from client', { timeoutMs: 2000 })
      await clientSendBi.finish()
      const fromClient = await serverRecvBi.readToEnd(65536, 2000)
      t.equal(
        Buffer.from(fromClient).toString('utf8'),
        'hi from client',
        'server read bi stream payload'
      )

      const stats = await clientConn.stats()
      t.equal(
        typeof stats.maxDatagramSize,
        'number',
        'stats include max datagram size'
      )

      const typeName = await watcherEvent
      t.ok(typeof typeName === 'string', 'received connection type update')

      if (typeof stopWatcher === 'function') {
        stopWatcher()
      }

      await clientConn.close()
      await serverConn.waitClosed()
    } finally {
      await client.close().catch(() => {})
      await server.close().catch(() => {})
    }
  })
})

test('iroh endpoint rejects invalid secret key', async (t) => {
  await withIroh(t, async () => {
    await t.rejects(
      iroh.Endpoint.create({ secretKey: 'invalid-secret-key' }),
      /secret key/i,
      'invalid secret key is rejected'
    )
  })
})

test('iroh endpoint rejects unsupported discovery mode', async (t) => {
  await withIroh(t, async () => {
    await t.rejects(
      iroh.Endpoint.create({ discovery: 'dns' }),
      /discovery/i,
      'unsupported discovery mode is rejected'
    )
  })
})
