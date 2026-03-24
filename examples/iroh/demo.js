// Run with the Oro Runtime CLI (desktop environment):
//   oroc run examples examples/iroh/demo.js

import { runIrohDemo, DEFAULT_ALPN } from './workflow.js'

function formatError (error) {
  if (!error) return 'Unknown error'
  if (typeof error === 'string') return error
  if (error?.message) return error.message
  return String(error)
}

function formatDuration (result) {
  if (!result?.startedAt || !result?.finishedAt) return null
  const ms = Math.max(0, result.finishedAt - result.startedAt)
  if (ms < 1000) return `${ms}ms`
  return `${(ms / 1000).toFixed(2)}s`
}

function printSummary (result) {
  console.log('--- connectivity summary ---')
  console.log(` server node address: ${result.serverAddr || 'n/a'}`)
  console.log(` client node address: ${result.clientAddr || 'n/a'}`)

  if (result.datagramRoundtrip) {
    console.log(
      ` datagram (client → server): ${result.datagramRoundtrip.clientToServer}`
    )
    console.log(
      ` datagram observed by server: ${result.datagramRoundtrip.serverObserved}`
    )
    console.log(
      ` datagram (server → client): ${result.datagramRoundtrip.serverToClient}`
    )
    console.log(
      ` datagram observed by client: ${result.datagramRoundtrip.clientObserved}`
    )
  }

  if (result.unidirectionalPayload) {
    console.log(` unidirectional payload: ${result.unidirectionalPayload}`)
  }

  if (result.bidirectionalPayload) {
    console.log(
      ` bidirectional server → client: ${result.bidirectionalPayload.serverToClient}`
    )
    console.log(
      ` bidirectional client → server: ${result.bidirectionalPayload.clientToServer}`
    )
  }

  if (result.stats) {
    const { maxDatagramSize, rtt, packetLoss } = result.stats
    console.log(' stats:')
    console.log(`   max datagram size: ${maxDatagramSize}`)
    console.log(`   rtt: ${rtt}`)
    console.log(`   packet loss: ${packetLoss}`)
  }

  if (result.connectionEvents?.length) {
    console.log(' connection events:')
    for (const event of result.connectionEvents) {
      if (event.type === 'connectionType') {
        console.log(`   connection type: ${event.name}`)
      } else if (event.type === 'error') {
        console.log(`   watcher error: ${event.message}`)
      }
    }
  }

  const duration = formatDuration(result)
  if (duration) {
    console.log(` duration: ${duration}`)
  }
  console.log(
    ` shutdown: ${result.shutdown === true ? 'service stopped' : result.shutdown === false ? 'already stopped' : 'unknown'}`
  )
}

async function main () {
  console.log('Starting iroh connectivity demo…')
  try {
    const result = await runIrohDemo({
      alpn: DEFAULT_ALPN,
      log: (line) => console.log(`[iroh] ${line}`)
    })
    printSummary(result)
    console.log('Demo complete')
  } catch (error) {
    const message = formatError(error)
    console.error(`Iroh demo failed: ${message}`)
    throw error
  }
}

main().catch((error) => {
  console.error(error)
  process.exitCode = 1
})
