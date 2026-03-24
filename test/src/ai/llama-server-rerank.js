import test from 'oro:test'

test('llama-server: rerank without model returns error', async (t) => {
  const payload = {
    query: 'hello',
    documents: ['hello world', 'another document']
  }
  const res = await fetch('/ai/llama/rerank', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(payload)
  })
  t.equal(res.status, 400, 'rerank returns 400 when no model is available')
  const text = await res.text()
  t.ok(
    /No model loaded|Model unavailable/.test(text),
    'error mentions missing model'
  )
})
