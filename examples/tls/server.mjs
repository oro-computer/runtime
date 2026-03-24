// Minimal TLS echo server using oro:tls.
// Run with:
//   ORO_ENABLE_MBEDTLS=1 oroc run examples examples/tls/server.mjs

import fs from 'oro:fs/promises'
import { createServer } from 'oro:tls'

async function main () {
  const [cert, key] = await Promise.all([
    fs.readFile(new URL('./server-cert.pem', import.meta.url), 'utf8'),
    fs.readFile(new URL('./server-key.pem', import.meta.url), 'utf8')
  ])

  const server = createServer(
    { cert, key, alpnProtocols: ['http/1.1'] },
    (socket) => {
      // Echo any received data back to the client
      socket.on('data', (chunk) => {
        try {
          socket.write(chunk)
        } catch (e) {
          console.error('write error', e)
        }
      })
      socket.on('end', () => console.log('client ended'))
      socket.on('error', (err) => console.error('client error:', err))
      socket.on('close', () => console.log('client closed'))
    }
  )

  server.on('secureConnectionReady', ({ clientId }) => {
    console.log('TLS secure for client', clientId)
  })
  server.on('error', (err) => console.error('server error:', err))
  server.on('close', () => console.log('server closed'))

  server.listen(9443, '127.0.0.1', 128, (err) => {
    if (err) console.error('listen error:', err)
    else console.log('TLS echo server listening on 127.0.0.1:9443')
  })

  const handleShutdown = (signal) => {
    console.log(`${signal} received, shutting down TLS server...`)
    try {
      server.close(() => {
        console.log('TLS server stopped')
        process.exit(0)
      })
    } catch (error) {
      console.error('error during shutdown:', error)
      process.exit(1)
    }
  }

  process.once('SIGINT', () => handleShutdown('SIGINT'))
  process.once('SIGTERM', () => handleShutdown('SIGTERM'))
}

main().catch((e) => {
  console.error('fatal:', e)
})
