import { test } from 'oro:test'
import process from 'oro:process'
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

test('tls: rejectUnauthorized true without CA should fail', async (t) => {
  if (!tlsEnabled()) {
    t.pass('TLS not enabled; skipping')
    return
  }
  const [cert, key] = await Promise.all([
    readFixture('server-cert.pem'),
    readFixture('server-key.pem')
  ])
  const port = 30446
  let server
  await new Promise((resolve, reject) => {
    server = createServer({ cert, key, alpnProtocols: ['http/1.1'] })
    server.on('error', reject)
    server.listen(port, '127.0.0.1', 128, (err) =>
      err ? reject(err) : resolve()
    )
  })
  const errp = new Promise((resolve) => {
    const s = connect({
      host: '127.0.0.1',
      port,
      servername: 'localhost',
      rejectUnauthorized: true,
      alpnProtocols: ['http/1.1']
    })
    s.on('error', (e) => {
      resolve(e)
    })
  })
  const err = await errp
  t.ok(err instanceof Error, 'handshake failed without CA')
  if (err && typeof err === 'object' && typeof err.code === 'string') {
    t.ok(
      ['CERT_UNTRUSTED', 'CERTIFICATE_VERIFY_FAILED'].includes(err.code),
      `code ok (${err.code})`
    )
  }
  await server.close()
})

test('tls: hostname mismatch fails verification', async (t) => {
  if (!tlsEnabled()) {
    t.pass('TLS not enabled; skipping')
    return
  }
  const [cert, key, ca] = await Promise.all([
    readFixture('server-cert.pem'),
    readFixture('server-key.pem'),
    readFixture('ca-cert.pem')
  ])
  const port = 30447
  let server
  await new Promise((resolve, reject) => {
    server = createServer({ cert, key, alpnProtocols: ['http/1.1'] })
    server.on('error', reject)
    server.listen(port, '127.0.0.1', 128, (err) =>
      err ? reject(err) : resolve()
    )
  })
  const errp = new Promise((resolve) => {
    // Intentionally wrong servername
    const s = connect({
      host: '127.0.0.1',
      port,
      servername: 'example.com',
      rejectUnauthorized: true,
      ca,
      alpnProtocols: ['http/1.1']
    })
    s.on('error', (e) => {
      resolve(e)
    })
  })
  const err = await errp
  t.ok(err instanceof Error, 'handshake fails on hostname mismatch')
  if (err && typeof err === 'object' && typeof err.code === 'string') {
    t.ok(
      ['HOSTNAME_MISMATCH', 'CERTIFICATE_VERIFY_FAILED'].includes(err.code),
      `code ok (${err.code})`
    )
  }
  await server.close()
})

test('tls: server requires client cert; client does not present cert', async (t) => {
  if (!tlsEnabled()) {
    t.pass('TLS not enabled; skipping')
    return
  }
  const [srvCert, srvKey, ca] = await Promise.all([
    readFixture('server-cert.pem'),
    readFixture('server-key.pem'),
    readFixture('ca-cert.pem')
  ])
  const port = 30448
  let server
  await new Promise((resolve, reject) => {
    server = createServer({
      cert: srvCert,
      key: srvKey,
      ca,
      requestCert: true,
      alpnProtocols: ['http/1.1']
    })
    server.on('error', reject)
    server.listen(port, '127.0.0.1', 128, (err) =>
      err ? reject(err) : resolve()
    )
  })
  const errp = new Promise((resolve) => {
    const s = connect({
      host: '127.0.0.1',
      port,
      servername: 'localhost',
      rejectUnauthorized: true,
      ca,
      alpnProtocols: ['http/1.1']
    })
    s.on('error', (e) => {
      resolve(e)
    })
  })
  const err = await errp
  t.ok(err instanceof Error, 'handshake fails when client cert required')
  if (err && typeof err === 'object' && typeof err.code === 'string') {
    t.ok(
      ['CERTIFICATE_VERIFY_FAILED'].includes(err.code),
      `code ok (${err.code})`
    )
  }
  await server.close()
})
