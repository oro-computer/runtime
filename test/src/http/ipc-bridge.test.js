import { test } from 'oro:test'
import application from 'oro:application'

const getBaseURL = () => {
  const host = application.config.http_ipc_host || '127.0.0.1'
  const port = Number(application.config.http_ipc_port || 0)
  const path = application.config.http_ipc_path || '/ipc'
  return { host, port, path, base: `http://${host}:${port}${path}` }
}

test.desktop('http.ipc: basic route via path mapping', async (t) => {
  const { base } = getBaseURL()
  t.ok(base.includes('http://'), 'base URL is http')

  const res = await fetch(`${base}/diagnostics.capabilities`, {
    headers: { 'X-IPC-Window-Index': '0' }
  })
  t.equal(res.status, 200, 'OK status')
  const json = await res.json()
  t.equal(json.source, 'diagnostics.capabilities', 'source matches route')
  t.ok(json.data?.streaming, 'has streaming capability payload')
})

test.desktop('http.ipc: header mapping via X-IPC-URI', async (t) => {
  const { base } = getBaseURL()
  const res = await fetch(base, {
    headers: {
      'X-IPC-URI': 'ipc://diagnostics.query?seq=0',
      'X-IPC-Window-Index': '0'
    }
  })
  t.equal(res.status, 200, 'OK status')
  const json = await res.json()
  t.equal(json.source, 'diagnostics.query', 'source matches route')
  t.ok(json.data, 'has data')
})

test.desktop('http.ipc: SSE stream terminates', async (t) => {
  const { base } = getBaseURL()
  const count = 3
  const res = await fetch(
    `${base}/diagnostics.stream.sse?count=${count}&interval=10`,
    {
      headers: { 'X-IPC-Window-Index': '0' }
    }
  )
  t.equal(res.status, 200, 'OK status')
  const text = await res.text()
  const lines = text.split(/\n/).filter(Boolean)
  const datalines = lines.filter((l) => l.startsWith('data: '))
  t.ok(datalines.length >= count, 'received expected data events')
})

test.desktop('http.ipc: chunked binary stream', async (t) => {
  const { base } = getBaseURL()
  const chunks = 4
  const size = 64
  const res = await fetch(
    `${base}/diagnostics.stream.chunks?chunks=${chunks}&chunkSize=${size}&interval=5`,
    {
      headers: { 'X-IPC-Window-Index': '0' }
    }
  )
  t.equal(res.status, 200, 'OK status')
  const buf = new Uint8Array(await res.arrayBuffer())
  t.ok(
    buf.byteLength >= chunks * size,
    'received expected chunked payload size'
  )
})
