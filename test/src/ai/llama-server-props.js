import test from 'oro:test'

test('llama-server: /props returns capability snapshot', async (t) => {
  const res = await fetch('/ai/llama/props')
  t.equal(res.status, 200, 'props returns 200')
  const json = await res.json().catch(() => ({}))
  t.ok(json.default_generation_settings, 'default generation settings present')
  t.ok(typeof json.total_slots === 'number', 'total_slots present')
  t.ok(
    json.modalities && typeof json.modalities === 'object',
    'modalities object present'
  )
  t.ok(
    json.build_info && typeof json.build_info === 'object',
    'build_info present'
  )
})

test('llama-server: POST /props returns not supported', async (t) => {
  const res = await fetch('/ai/llama/props', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({})
  })
  t.equal(res.status, 501, 'props mutation not supported by default')
  const text = await res.text()
  t.ok(/not supported/i.test(text), 'not supported message present')
})
