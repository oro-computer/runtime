import test from 'oro:test'
import process from 'oro:process'

function decodeText (uint8) {
  try {
    return new TextDecoder().decode(uint8)
  } catch {
    return ''
  }
}

async function maybeLoadModel (t) {
  const name = process.env.ORO_AI_MODEL_NAME
  if (!name) return false
  const dir = process.env.ORO_AI_MODEL_DIR || ''
  const qs = new URLSearchParams({ name })
  if (dir) qs.set('directory', dir)
  const res = await fetch('ipc://ai.llm.model.load?' + qs.toString())
  const body = await res.text()
  t.comment('model load: ' + body)
  return res.ok
}

test('llama-server: model E2E (conditional)', async (t) => {
  const loaded = await maybeLoadModel(t).catch(() => false)
  if (!loaded) {
    return t.comment(
      'ORO_AI_MODEL_NAME not set or failed to load — skipping E2E'
    )
  }

  // Non-stream chat
  const chatRes = await fetch('/ai/llama/v1/chat/completions', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({
      messages: [{ role: 'user', content: 'Briefly say hi' }],
      max_tokens: 64
    })
  })
  t.equal(chatRes.status, 200, 'chat non-stream returns 200')
  const chatJson = await chatRes.json()
  t.ok(Array.isArray(chatJson?.choices), 'choices present')

  // Stream with stop sequence
  const url = '/ai/llama/v1/chat/completions?stream=true&stop=###&max_tokens=64'
  const streamRes = await fetch(url, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({
      messages: [{ role: 'user', content: 'Say hello' }]
    })
  })
  t.equal(streamRes.status, 200, 'chat stream returns 200')
  const reader = streamRes.body.getReader()
  let gotData = false
  let chunks = ''
  const start = Date.now()
  while (Date.now() - start < 8000) {
    // 8s safety window
    const { done, value } = await reader.read()
    if (done) break
    const text = decodeText(value)
    chunks += text
    if (/^data: /m.test(text)) gotData = true
    if (chunks.includes('[DONE]')) break
  }
  t.ok(gotData, 'received SSE data frames')
  t.ok(chunks.includes('[DONE]'), 'stream terminated with [DONE]')
})
