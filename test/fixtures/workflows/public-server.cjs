// Public loopback qualification server only; never a deployment or authentication service.
const fs = require('node:fs')
const https = require('node:https')
const path = require('node:path')
const assert = require('node:assert/strict')
assert.ok(process.env.ORO_PUBLIC_WORKFLOW_STATE, 'Select a private writable public-fixture directory')
const base = path.resolve(process.env.ORO_PUBLIC_WORKFLOW_STATE)
let mode = 'normal'
let generation = 1
const stats = { requests: 0, bearer: 0, cancelled: 0 }
const server = https.createServer({ key: fs.readFileSync(base + '/public-key.pem'), cert: fs.readFileSync(base + '/public-cert.pem') }, (req, res) => {
  if (req.url.startsWith('/control')) {
    const query = new URL(req.url, 'https://localhost').searchParams
    if (query.get('mode') === 'shutdown') { res.end('{}'); server.close(); return }
    mode = query.get('mode') || mode
    generation = Number(query.get('generation') || generation)
    res.end(JSON.stringify({ mode, generation, ...stats }))
    return
  }
  stats.requests++
  if (req.headers.authorization === 'Bearer public-test-bearer') stats.bearer++
  if (mode === 'slow') { req.on('close', () => stats.cancelled++); setTimeout(() => { if (!res.destroyed) res.end('{}') }, 55000); return }
  if (mode === 'timeout') {
    const timer = setTimeout(() => { if (!res.destroyed) res.end('{}') }, 20000).unref()
    res.once('close', () => { clearTimeout(timer); if (!res.writableEnded) stats.cancelled++ })
    return
  }
  if (mode === 'unauthorized') { res.writeHead(401); res.end('{}'); return }
  res.setHeader('content-type', 'application/json')
  res.end(JSON.stringify({ alerts: [{ id: 'public-event-' + generation, accountRef: mode === 'wrong-scope' ? 'other' : 'public-account', targetRef: 'public-target', title: 'Public workflow ' + generation, body: 'Public test data only' }], cursor: 'public-' + generation }))
})
const port = Number(process.env.ORO_PUBLIC_WORKFLOW_PORT || 0)
assert.ok(Number.isInteger(port) && port >= 0 && port <= 65535)
server.listen(port, '127.0.0.1', () => fs.writeFileSync(base + '/public-port.txt', String(server.address().port)))
