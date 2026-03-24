import test from 'oro:test'

test('llama-server: /metrics exposes prometheus text', async (t) => {
  const res = await fetch('/ai/llama/metrics')
  t.equal(res.status, 200, 'metrics returns 200')
  const text = await res.text()
  t.ok(text.includes('oro_llm_models_loaded'), 'contains models metric')
  t.ok(
    text.includes('# TYPE oro_llm_context_pool_size gauge'),
    'contains context pool metric definition'
  )
})
