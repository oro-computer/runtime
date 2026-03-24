import { test } from 'oro:test'
import process from 'oro:process'
import Buffer from 'oro:buffer'
import fs from 'oro:fs/promises'
import { createServer, connect } from 'oro:tls'

const fixtures = new URL('../../fixtures/tls/', import.meta.url)
async function readFixture (name) {
  return fs.readFile(new URL(name, fixtures), 'utf8')
}

function tlsEnabled () {
  return (
    process.env.ORO_ENABLE_TLS === '1' || process.env.ORO_ENABLE_MBEDTLS === '1'
  )
}

test('tls: passphrase-protected server and client keys', async (t) => {
  if (!tlsEnabled()) {
    t.pass('TLS not enabled; skipping')
    return
  }

  const [srvCert, srvKeyPass, cliCert, cliKeyPass, ca] = await Promise.all([
    readFixture('server-cert.pem'),
    readFixture('server-key-pass.pem'),
    readFixture('client-cert.pem'),
    readFixture('client-key-pass.pem'),
    readFixture('ca-cert.pem')
  ])

  const port = 30449
  let server

  await new Promise((resolve, reject) => {
    try {
      server = createServer(
        {
          cert: srvCert,
          key: srvKeyPass,
          keyPassphrase: 'orosecret',
          ca,
          requestCert: true,
          alpnProtocols: ['http/1.1']
        },
        (socket) => {
          socket.on('data', (chunk) => {
            try {
              socket.write(chunk)
            } catch (err) {
              reject(err)
            }
          })
        }
      )
      server.on('error', reject)
      server.on('secureConnection', (info) => {
        t.equal(info?.alpnProtocol, 'http/1.1', 'server negotiated ALPN')
      })
      server.listen(port, '127.0.0.1', 128, (err) =>
        err ? reject(err) : resolve()
      )
    } catch (err) {
      reject(err)
    }
  })

  const received = new Promise((resolve, reject) => {
    const sock = connect({
      host: '127.0.0.1',
      port,
      servername: 'localhost',
      rejectUnauthorized: true,
      ca,
      cert: cliCert,
      key: cliKeyPass,
      keyPassphrase: 'clientsecret',
      alpnProtocols: ['http/1.1']
    })
    sock.on('error', reject)
    sock.on('secureConnect', (info) => {
      t.equal(info?.alpnProtocol, 'http/1.1', 'client negotiated ALPN')
      try {
        sock.write('passphrase echo')
      } catch (err) {
        reject(err)
      }
    })
    sock.on('data', (buf) => {
      const value = Buffer.from(buf).toString('utf8')
      t.equal(value, 'passphrase echo', 'roundtrip payload matches')
      sock.end()
      resolve()
    })
  })

  await received
  await server.close()
})
