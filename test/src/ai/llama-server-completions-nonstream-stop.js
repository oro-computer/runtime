import test from 'oro:test'

test('llama-server: /v1/completions non-stream (conditional)', async (t) => {
  // Only run when a model is loaded
  const modelsRes = await fetch('/ai/llama/v1/models')
  t.equal(modelsRes.status, 200, 'models returns 200')
  const modelsJson = await modelsRes.json().catch(() => ({ data: [] }))
  const model = Array.isArray(modelsJson?.data) && modelsJson.data[0]
  if (!model) {
    t.ok(true, 'no model loaded — skipping non-stream completions test')
    return
  }

  const url = `/ai/llama/v1/completions?prompt=Hello&max_tokens=32&stop=###&model=${encodeURIComponent(model.id)}`
  const res = await fetch(url)
  t.equal(
    res.status,
    200,
    'non-stream completions returns 200 when model is loaded'
  )
  const json = await res.json().catch(() => ({}))
  t.equal(json.object, 'text_completion', 'object is text_completion')
  t.ok(Array.isArray(json.choices), 'choices array present')
  t.ok(
    json.usage && typeof json.usage.total_tokens === 'number',
    'usage present'
  )
})
