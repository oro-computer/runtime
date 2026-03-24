import test from 'oro:test'

test('llama-server: lora-adapters returns empty list without adapters', async (t) => {
  const res = await fetch('/ai/llama/lora-adapters')
  t.equal(res.status, 200, 'lora-adapters returns 200')
  const json = await res.json().catch(() => ({}))
  t.equal(json.object, 'list', 'object list')
  t.ok(Array.isArray(json.data), 'data is array')
})

test('llama-server: lora-adapters POST requires known adapters', async (t) => {
  const res = await fetch('/ai/llama/lora-adapters', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: '[]'
  })
  t.equal(res.status, 400, 'POST fails without configured adapters')
})
