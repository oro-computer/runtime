import test from 'oro:test'

test('llama-server: slots GET returns list', async (t) => {
  const res = await fetch('/ai/llama/slots')
  t.equal(res.status, 200, 'slots GET returns 200')
  const json = await res.json().catch(() => ({}))
  t.equal(json.object, 'list', 'object list')
  t.ok(Array.isArray(json.data), 'data is array')
  t.ok(json.totals && typeof json.totals.total === 'number', 'totals present')
})

test('llama-server: slots release non-existent returns conflict', async (t) => {
  const res = await fetch('/ai/llama/slots/999999', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ action: 'release' })
  })
  t.equal(res.status, 409, 'release missing slot returns conflict')
})

test('llama-server: slots drain without model is a no-op', async (t) => {
  const res = await fetch('/ai/llama/slots', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ action: 'drain' })
  })
  t.equal(res.status, 200, 'drain without model succeeds')
  const json = await res.json().catch(() => ({}))
  t.equal(json.dropped, 0, 'dropped defaults to zero')
})
