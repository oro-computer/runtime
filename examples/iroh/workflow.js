import * as iroh from 'oro:iroh'
import Buffer from 'oro:buffer'

export const DEFAULT_ALPN = 'oro-iroh-demo'
const DEFAULT_TIMEOUT_MS = 4000

function defaultLog (line) {
  console.log(`[iroh] ${line}`)
}

function toUtf8 (input) {
  if (!input) return ''
  try {
    return Buffer.from(input).toString('utf8')
  } catch {
    return String(input)
  }
}

function extractNodeId (addr) {
  if (!addr) return ''
  const match = addr.match(/[A-Z2-7]{52}/)
  return match ? match[0] : ''
}

function formatErrorMessage (error) {
  if (!error) return 'Unknown error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  return String(error)
}

export async function runIrohDemo (options = {}) {
  const log = options.log || defaultLog
  const timeoutMs = options.timeoutMs || DEFAULT_TIMEOUT_MS
  const alpn = options.alpn || DEFAULT_ALPN
  const discovery = options.discovery || 'mdns'
  const relayMode = options.relayMode || 'default'
  const logLevel = options.logLevel || 'info'
  const includeWatcher = options.includeWatcher !== false
  const datagramOutbound = options.datagramOutbound || 'ping from client'
  const datagramInbound = options.datagramInbound || 'pong from server'
  const uniPayload = options.uniPayload || 'hello from client (uni stream)'
  const biServerPayload =
    options.biServerPayload || 'greetings from server (bi stream)'
  const biClientPayload =
    options.biClientPayload || 'hello back from client (bi stream)'
  const connectionEvents = []

  const resources = {
    server: null,
    client: null,
    clientConn: null,
    serverConn: null,
    watcherStop: null
  }

  const result = {
    startedAt: Date.now(),
    initialStatus: null,
    readyStatus: null,
    serverAddr: '',
    clientAddr: '',
    serverRelay: null,
    clientRelay: null,
    datagramRoundtrip: null,
    unidirectionalPayload: '',
    bidirectionalPayload: null,
    stats: null,
    shutdown: null,
    finishedAt: null,
    connectionEvents
  }

  async function cleanup () {
    if (resources.watcherStop) {
      try {
        resources.watcherStop()
      } catch (err) {
        log(`Failed to stop connection watcher: ${formatErrorMessage(err)}`)
      }
      resources.watcherStop = null
    }

    if (resources.clientConn) {
      try {
        await resources.clientConn.close()
      } catch (err) {
        log(`clientConn.close failed: ${formatErrorMessage(err)}`)
      }
    }

    if (resources.serverConn) {
      try {
        await resources.serverConn.close()
      } catch (err) {
        log(`serverConn.close failed: ${formatErrorMessage(err)}`)
      }
      try {
        await resources.serverConn.waitClosed()
      } catch (err) {
        log(`serverConn.waitClosed failed: ${formatErrorMessage(err)}`)
      }
    }

    if (resources.client) {
      try {
        await resources.client.close()
      } catch (err) {
        log(`client.close failed: ${formatErrorMessage(err)}`)
      }
    }

    if (resources.server) {
      try {
        await resources.server.close()
      } catch (err) {
        log(`server.close failed: ${formatErrorMessage(err)}`)
      }
    }

    try {
      result.shutdown = await iroh.shutdown()
      if (result.shutdown) {
        log('Iroh service shut down')
      } else {
        log('Iroh service already inactive')
      }
    } catch (err) {
      log(`iroh.shutdown failed: ${formatErrorMessage(err)}`)
      result.shutdown = false
    }
  }

  try {
    log('Checking iroh status…')
    result.initialStatus = await iroh.status().catch((err) => {
      if (err?.message && err.message.includes('Iroh service is disabled')) {
        throw new Error(
          'Iroh service is disabled. Rebuild the runtime with iroh support enabled.'
        )
      }
      throw err
    })

    if (!result.initialStatus.initialized) {
      log('Initialising iroh service…')
    } else {
      log('Iroh service already initialised')
    }

    result.readyStatus = await iroh.ensureInitialized()
    log(`Iroh version ${result.readyStatus.version}`)

    const level = await iroh.setLogLevel(logLevel)
    log(`Log level set to ${level.name}`)

    log('Creating server endpoint…')
    resources.server = await iroh.Endpoint.create({
      discovery,
      relayMode,
      alpns: [alpn]
    })
    await resources.server.bind()
    result.serverAddr = await resources.server.nodeAddr()
    log(`Server node address: ${result.serverAddr}`)
    try {
      result.serverRelay = await resources.server.homeRelay()
      if (result.serverRelay) {
        log(`Server home relay: ${result.serverRelay}`)
      }
    } catch (err) {
      log(`Failed to resolve server relay: ${formatErrorMessage(err)}`)
    }

    log('Creating client endpoint…')
    resources.client = await iroh.Endpoint.create({ alpns: [alpn] })
    await resources.client.bind()
    result.clientAddr = await resources.client.nodeAddr()
    log(`Client node address: ${result.clientAddr}`)
    try {
      result.clientRelay = await resources.client.homeRelay()
      if (result.clientRelay) {
        log(`Client home relay: ${result.clientRelay}`)
      }
    } catch (err) {
      log(`Failed to resolve client relay: ${formatErrorMessage(err)}`)
    }

    log('Awaiting incoming connection on server…')
    const serverAcceptPromise = resources.server.accept({ expectedAlpn: alpn })
    log('Dialling server from client…')
    resources.clientConn = await resources.client.connect({
      nodeAddr: result.serverAddr,
      alpn
    })
    resources.serverConn = await serverAcceptPromise
    log('Connections established')

    if (resources.serverConn.remoteAlpn) {
      const remoteAlpn = toUtf8(resources.serverConn.remoteAlpn)
      log(`Server observed ALPN: ${remoteAlpn}`)
    }

    if (typeof options.onConnectionEstablished === 'function') {
      await options.onConnectionEstablished({
        clientConn: resources.clientConn,
        serverConn: resources.serverConn
      })
    }

    if (includeWatcher) {
      const clientNodeId = extractNodeId(result.clientAddr)
      if (clientNodeId) {
        log(`Watching connection type for ${clientNodeId}…`)
        try {
          resources.watcherStop =
            await resources.serverConn.watchConnectionType(
              clientNodeId,
              (err, info) => {
                if (err) {
                  const message = formatErrorMessage(err)
                  log(`Connection type watcher error: ${message}`)
                  connectionEvents.push({
                    type: 'error',
                    message,
                    at: Date.now()
                  })
                  if (typeof options.onWatcherEvent === 'function') {
                    options.onWatcherEvent({ type: 'error', message })
                  }
                  return
                }
                const typeName = info?.connectionType?.name || 'unknown'
                log(`Connection type update: ${typeName}`)
                const event = {
                  type: 'connectionType',
                  name: typeName,
                  at: Date.now()
                }
                connectionEvents.push(event)
                if (typeof options.onWatcherEvent === 'function') {
                  options.onWatcherEvent(event)
                }
              }
            )
        } catch (err) {
          log(
            `Failed to register connection watcher: ${formatErrorMessage(err)}`
          )
        }
      } else {
        log('Client node id unavailable; skipping connection watcher')
      }
    }

    log('Sending datagram from client…')
    await resources.clientConn.writeDatagram(datagramOutbound, { timeoutMs })
    const serverDatagram = await resources.serverConn.readDatagram({
      timeoutMs
    })
    const serverText = toUtf8(serverDatagram)
    log(`Server received datagram: ${serverText}`)

    log('Responding with datagram from server…')
    await resources.serverConn.writeDatagram(datagramInbound, { timeoutMs })
    const clientDatagram = await resources.clientConn.readDatagram({
      timeoutMs
    })
    const clientText = toUtf8(clientDatagram)
    log(`Client received datagram: ${clientText}`)
    result.datagramRoundtrip = {
      clientToServer: datagramOutbound,
      serverObserved: serverText,
      serverToClient: datagramInbound,
      clientObserved: clientText
    }

    log('Opening unidirectional stream from client…')
    const serverRecvPromise = resources.serverConn.acceptUnidirectionalStream()
    const uniSend = await resources.clientConn.openUnidirectionalStream()
    await uniSend.write(uniPayload, { timeoutMs })
    await uniSend.finish()
    const uniRecv = await serverRecvPromise
    const uniBuffer = await uniRecv.readToEnd(65536, timeoutMs)
    result.unidirectionalPayload = toUtf8(uniBuffer)
    log(`Server read unidirectional payload: ${result.unidirectionalPayload}`)

    log('Opening bidirectional stream from server…')
    const clientPairPromise = resources.clientConn.acceptBidirectionalStream()
    const [serverSend, serverRecv] =
      await resources.serverConn.openBidirectionalStream()
    const [clientSend, clientRecv] = await clientPairPromise

    await serverSend.write(biServerPayload, { timeoutMs })
    await serverSend.finish()
    const fromServer = await clientRecv.readToEnd(65536, timeoutMs)
    const serverToClient = toUtf8(fromServer)
    log(`Client read bidirectional payload: ${serverToClient}`)

    await clientSend.write(biClientPayload, { timeoutMs })
    await clientSend.finish()
    const fromClient = await serverRecv.readToEnd(65536, timeoutMs)
    const clientToServer = toUtf8(fromClient)
    log(`Server read bidirectional payload: ${clientToServer}`)
    result.bidirectionalPayload = {
      serverToClient,
      clientToServer
    }

    result.stats = await resources.clientConn.stats()
    if (result.stats) {
      const { maxDatagramSize, rtt, packetLoss } = result.stats
      log(
        `Connection stats → maxDatagramSize=${maxDatagramSize} rtt=${rtt} packetLoss=${packetLoss}`
      )
    }

    if (resources.watcherStop && connectionEvents.length === 0) {
      log('No connection type updates received during run')
    }

    result.finishedAt = Date.now()
    return result
  } catch (error) {
    const message = formatErrorMessage(error)
    log(`Iroh demo failed: ${message}`)
    throw error
  } finally {
    await cleanup()
  }
}

export default runIrohDemo
