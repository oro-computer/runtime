import { test } from 'oro:test'
import process from 'oro:process'
import fs from 'oro:fs/promises'
import path from 'oro:path'
import { Buffer } from 'oro:buffer'
import { connect, createServer, getTlsProvider } from 'oro:tls'
import FIXTURES from '../fixtures.js'

test('tls: SDK application registers its static provider', async (t) => {
  if (process.platform !== 'linux') {
    t.skip('Linux static TLS provider regression')
    return
  }

  // Run through the normal SDK application build, without a native registration shim
  // or environment-variable gate that could hide missing provider registration.
  const { provider } = await getTlsProvider()
  t.ok(['mbedtls', 'openssl'].includes(provider), 'built-in provider selected')

  const [cert, key, ca] = await Promise.all(
    ['server-cert.pem', 'server-key.pem', 'ca-cert.pem'].map((name) =>
      fs.readFile(path.join(FIXTURES, 'tls', name), 'utf8')
    )
  )

  const server = createServer({ cert, key })
  const port = 30450
  let client
  let timer

  try {
    await new Promise((resolve, reject) => {
      timer = setTimeout(() => reject(new Error('TLS provider loopback timed out')), 10000)
      server.on('error', reject)
      server.on('connection', (socket) => {
        socket.on('error', reject)
        socket.on('data', (chunk) => socket.write(chunk))
      })
      server.listen(port, '127.0.0.1', 128, (err) => {
        if (err) return reject(err)

        client = connect({
          host: '127.0.0.1',
          port,
          servername: 'localhost',
          ca,
          rejectUnauthorized: true
        })
        client.on('error', reject)
        client.once('secureConnect', (info) => {
          t.equal(info.provider, provider, 'handshake uses the selected provider')
          t.ok(info.protocol && info.cipher, 'verified TLS handshake completes')
          client.write('static provider')
        })
        let received = ''
        client.on('data', (chunk) => {
          received += Buffer.from(chunk).toString('utf8')
          if (received.length >= 'static provider'.length) {
            t.equal(received, 'static provider', 'encrypted loopback payload matches')
            resolve()
          }
        })
      })
    })
  } finally {
    clearTimeout(timer)
    client?.destroy()
    await server.close()
  }
})
