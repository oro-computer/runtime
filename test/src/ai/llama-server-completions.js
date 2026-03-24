import test from 'oro:test'

function decodeText (uint8) {
  try {
    return new TextDecoder().decode(uint8)
  } catch {
    return ''
  }
}

test('llama-server: /v1/completions non-stream without model', async (t) => {
  const res = await fetch('/ai/llama/v1/completions?prompt=hello')
  const text = await res.text()
  t.equal(res.status, 400, 'completions returns 400 without model')
  t.ok(/No model loaded/.test(text), 'error message mentions no model loaded')
})
test('llama-server: /v1/completions SSE via query param without model', async (t) => {
  const url = '/ai/llama/v1/completions?prompt=hello&stream=true'
  const res = await fetch(url)
  t.equal(res.status, 200, 'SSE returns 200')
  t.ok(
    (res.headers.get('content-type') || '').includes('text/event-stream'),
    'content-type is event-stream'
  )
  const reader = res.body.getReader()
  let chunks = ''
  while (true) {
    const { done, value } = await reader.read()
    if (done) break
    chunks += decodeText(value)
    if (chunks.includes('[DONE]')) break
    if (chunks.length > 4096) break
  }
  t.ok(/data:/.test(chunks), 'received at least one SSE event')
})

test('llama-server: /v1/completions SSE via JSON body without model', async (t) => {
  const url = '/ai/llama/v1/completions'
  const body = JSON.stringify({ stream: true, prompt: 'hello' })
  const res = await fetch(url, {
    method: 'POST',
    body,
    headers: { 'content-type': 'application/json' }
  })
  t.equal(res.status, 200, 'SSE returns 200')
  t.ok(
    (res.headers.get('content-type') || '').includes('text/event-stream'),
    'content-type is event-stream'
  )
  const reader = res.body.getReader()
  let chunks = ''
  while (true) {
    const { done, value } = await reader.read()
    if (done) break
    chunks += decodeText(value)
    if (chunks.includes('[DONE]')) break
    if (chunks.length > 4096) break
  }
  t.ok(/data:/.test(chunks), 'received at least one SSE event')
})

test('llama-server: stop sequences (conditional)', async (t) => {
  const modelsRes = await fetch('/ai/llama/v1/models')
  const modelsJson = await modelsRes.json().catch(() => ({ data: [] }))
  if (!Array.isArray(modelsJson?.data) || modelsJson.data.length === 0) {
    t.ok(true, 'no model loaded — skipping stop sequences test')
    return
  }

  const url =
    '/ai/llama/v1/chat/completions?stream=true&prompt=Say%20hi&stop=###&max_tokens=64'
  const res = await fetch(url)
  t.equal(res.status, 200, 'SSE returns 200 when model is loaded')
  const reader = res.body.getReader()
  let chunks = ''
  let gotData = false
  const start = Date.now()
  while (Date.now() - start < 5000) {
    // 5s safety
    const { done, value } = await reader.read()
    if (done) break
    const text = decodeText(value)
    chunks += text
    if (/^data: /m.test(text)) gotData = true
    if (chunks.includes('[DONE]')) break
  }
  t.ok(gotData, 'received SSE data frames')
  t.ok(chunks.includes('[DONE]'), 'stream terminated')
})
