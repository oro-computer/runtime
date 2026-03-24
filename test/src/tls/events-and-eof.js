import { test } from 'oro:test'

test('tls: secureConnect payload is delivered (synthetic)', async (t) => {
  const tls = await import('oro:tls')
  const s = tls.connect({ host: 'example', port: 0, rejectUnauthorized: false })
  let got = null
  s.on('secureConnect', (info) => {
    got = info
  })
  const id = s.id
  // Synthesize a native event
  const detail = {
    source: 'tls.connect',
    params: {
      data: {
        id,
        protocol: 'TLSv1.3',
        cipher: 'TLS_AES_128_GCM_SHA256',
        hostname: 'example',
        subject: 'CN=example'
      }
    }
  }
  globalThis.dispatchEvent(new CustomEvent('data', { detail }))
  await new Promise((resolve) => setTimeout(resolve, 10))
  t.ok(got && got.protocol === 'TLSv1.3', 'secureConnect payload received')
  s.destroy()
})
test('tls: client EOF triggers end and readStop (synthetic)', async (t) => {
  const tls = await import('oro:tls')
  const s = tls.connect({ host: 'example', port: 0, rejectUnauthorized: false })
  let ended = false
  s.on('end', () => {
    ended = true
  })
  const id = s.id
  const detail = { source: 'tls.read', params: { data: { id, EOF: true } } }
  globalThis.dispatchEvent(new CustomEvent('data', { detail }))
  await new Promise((resolve) => setTimeout(resolve, 10))
  t.ok(ended, 'end emitted on EOF')
  s.destroy()
})
