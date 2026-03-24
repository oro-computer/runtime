import test from 'oro:test'

const body = {
  messages: [
    { role: 'system', content: 'You are a helper.' },
    { role: 'user', content: 'Hello there' }
  ]
}

test('llama-server: apply-template requires model', async (t) => {
  const res = await fetch('/ai/llama/apply-template', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(body)
  })
  t.equal(res.status, 400, 'apply-template returns 400 when no model available')
  const text = await res.text()
  t.ok(/No model loaded|Unable/.test(text), 'error describes missing model')
})
