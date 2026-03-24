// Mutual TLS client connecting to the local echo server.
// Run with:
//   ORO_ENABLE_MBEDTLS=1 oroc run examples examples/tls/client-mtls.mjs

import Buffer from 'oro:buffer'
import fs from 'oro:fs/promises'
import { connect } from 'oro:tls'

async function main () {
  const [cert, key, ca] = await Promise.all([
    fs.readFile(new URL('./client-cert.pem', import.meta.url), 'utf8'),
    fs.readFile(new URL('./client-key.pem', import.meta.url), 'utf8'),
    fs.readFile(new URL('./ca-cert.pem', import.meta.url), 'utf8')
  ])

  const socket = connect({
    host: '127.0.0.1',
    port: 9444,
    rejectUnauthorized: true,
    servername: 'localhost',
    alpnProtocols: ['http/1.1'],
    cert,
    key,
    ca
  })

  socket.on('secureConnect', (info) => {
    console.log('secureConnect (mTLS):', info)
    socket.write('hello over mtls')
  })

  socket.on('data', (buf) => {
    console.log('echo (mTLS):', Buffer.from(buf).toString('utf8'))
    socket.end()
  })

  socket.on('end', () => console.log('ended (mTLS)'))
  socket.on('close', () => console.log('closed (mTLS)'))
  socket.on('error', (err) => console.error('client error (mTLS):', err))
}

main().catch((e) => console.error('fatal:', e))
