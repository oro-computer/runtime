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
  // Build-time env mirrored at runtime in CI; gated when available
  return (
    process.env.ORO_ENABLE_TLS === '1' || process.env.ORO_ENABLE_MBEDTLS === '1'
  )
}

test('tls echo (self-signed, rejectUnauthorized:false)', async (t) => {
  if (!tlsEnabled()) {
    t.pass('TLS not enabled; skipping')
    return
  }

  const [cert, key] = await Promise.all([
    readFixture('server-cert.pem'),
    readFixture('server-key.pem')
  ])

  const port = 30443
  let server
  let closed = false

  await new Promise((resolve, reject) => {
    try {
      server = createServer(
        { cert, key, alpnProtocols: ['http/1.1'] },
        (socket) => {
          socket.on('data', (chunk) => {
            try {
              socket.write(chunk)
            } catch (err) {
              t.fail(err, 'server write error')
            }
          })
        }
      )
      server.on('secureConnection', (info) => {
        t.equal(info?.alpnProtocol, 'http/1.1', 'server reports ALPN http/1.1')
      })
      server.on('error', reject)
      server.listen(port, '127.0.0.1', 128, (err) =>
        err ? reject(err) : resolve()
      )
    } catch (e) {
      reject(e)
    }
  })

  const received = new Promise((resolve, reject) => {
    const sock = connect({
      host: '127.0.0.1',
      port,
      servername: 'localhost',
      rejectUnauthorized: false,
      alpnProtocols: ['http/1.1']
    })
    sock.on('error', reject)
    sock.on('secureConnect', (info) => {
      t.ok(info && typeof info === 'object', 'secureConnect info delivered')
      t.equal(info?.alpnProtocol, 'http/1.1', 'ALPN negotiated to http/1.1')
      try {
        sock.write('hello')
      } catch (e) {
        reject(e)
      }
    })
    sock.on('data', (buf) => {
      const s = Buffer.from(buf).toString('utf8')
      t.equal(s, 'hello', 'echo payload matches')
      sock.end()
      resolve()
    })
  })

  await received
  await server.close(() => {
    closed = true
  })
  t.ok(closed, 'server closed')
})

test('tls echo (CA trusted, rejectUnauthorized:true)', async (t) => {
  if (!tlsEnabled()) {
    t.pass('TLS not enabled; skipping')
    return
  }

  const [cert, key, ca] = await Promise.all([
    readFixture('server-cert.pem'),
    readFixture('server-key.pem'),
    readFixture('ca-cert.pem')
  ])

  const port = 30444
  let server

  await new Promise((resolve, reject) => {
    try {
      server = createServer(
        { cert, key, alpnProtocols: ['http/1.1'] },
        (socket) => {
          socket.on('data', (chunk) => {
            try {
              socket.write(chunk)
            } catch (e) {
              t.fail(e, 'server write error')
            }
          })
        }
      )
      server.on('error', reject)
      server.listen(port, '127.0.0.1', 128, (err) =>
        err ? reject(err) : resolve()
      )
    } catch (e) {
      reject(e)
    }
  })

  const received = new Promise((resolve, reject) => {
    const sock = connect({
      host: '127.0.0.1',
      port,
      servername: 'localhost',
      rejectUnauthorized: true,
      ca,
      alpnProtocols: ['http/1.1']
    })
    sock.on('error', reject)
    sock.on('secureConnect', (info) => {
      t.ok(info?.protocol && info?.cipher, 'protocol and cipher reported')
      t.equal(info?.alpnProtocol, 'http/1.1', 'ALPN negotiated to http/1.1')
      try {
        sock.write('world')
      } catch (e) {
        reject(e)
      }
    })
    sock.on('data', (buf) => {
      const s = Buffer.from(buf).toString('utf8')
      t.equal(s, 'world', 'echo payload matches')
      sock.end()
      resolve()
    })
  })

  await received
  await server.close()
})

test('tls mTLS (server requires client cert, client presents cert/key)', async (t) => {
  if (!tlsEnabled()) {
    t.pass('TLS not enabled; skipping')
    return
  }

  const [srvCert, srvKey, ca, cliCert, cliKey] = await Promise.all([
    readFixture('server-cert.pem'),
    readFixture('server-key.pem'),
    readFixture('ca-cert.pem'),
    readFixture('client-cert.pem'),
    readFixture('client-key.pem')
  ])

  const port = 30445
  let server
  await new Promise((resolve, reject) => {
    try {
      server = createServer(
        {
          cert: srvCert,
          key: srvKey,
          ca,
          requestCert: true,
          alpnProtocols: ['http/1.1']
        },
        (socket) => {
          socket.on('data', (chunk) => {
            try {
              socket.write(chunk)
            } catch (e) {
              t.fail(e, 'server write error')
            }
          })
        }
      )
      server.on('error', reject)
      server.listen(port, '127.0.0.1', 128, (err) =>
        err ? reject(err) : resolve()
      )
    } catch (e) {
      reject(e)
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
      key: cliKey,
      alpnProtocols: ['http/1.1']
    })
    sock.on('error', reject)
    sock.on('secureConnect', (info) => {
      t.equal(info?.alpnProtocol, 'http/1.1', 'ALPN negotiated to http/1.1')
      try {
        sock.write('mtls')
      } catch (e) {
        reject(e)
      }
    })
    sock.on('data', (buf) => {
      const s = Buffer.from(buf).toString('utf8')
      t.equal(s, 'mtls', 'echo payload matches (mTLS)')
      sock.end()
      resolve()
    })
  })

  await received
  await server.close()
})
