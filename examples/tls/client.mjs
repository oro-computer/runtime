// Minimal TLS client connecting to the local echo server.
// Run with:
//   ORO_ENABLE_MBEDTLS=1 oroc run examples examples/tls/client.mjs

import Buffer from 'oro:buffer'
import { connect } from 'oro:tls'

async function main () {
  const socket = connect({
    host: '127.0.0.1',
    port: 9443,
    // Self-signed local testing only; for production, keep true and provide CA
    rejectUnauthorized: false,
    servername: 'localhost',
    alpnProtocols: ['http/1.1']
  })

  socket.on('secureConnect', (info) => {
    console.log('secureConnect:', info)
    socket.write('hello over tls')
  })

  socket.on('data', (buf) => {
    console.log('echo:', Buffer.from(buf).toString('utf8'))
    socket.end()
  })

  socket.on('end', () => console.log('ended'))
  socket.on('close', () => console.log('closed'))
  socket.on('error', (err) => console.error('client error:', err))
}

main().catch((e) => console.error('fatal:', e))
