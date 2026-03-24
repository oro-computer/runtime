import test from 'oro:test'

function decodeText (uint8) {
  try {
    return new TextDecoder().decode(uint8)
  } catch {
    return ''
  }
}

test('llama-server: health endpoint', async (t) => {
  const res = await fetch('/ai/llama/health')
  t.equal(res.status, 200, 'health returns 200')
  const json = await res.json()
  t.equal(json.status, 'ok', 'status ok')
  t.ok(typeof json.llmInitialized === 'boolean', 'llmInitialized present')
})

test('llama-server: absolute socket URL works', async (t) => {
  const origin = globalThis.location?.origin || ''
  if (!origin) {
    t.comment('location.origin unavailable — skipping absolute URL check')
    return
  }

  const res = await fetch(`${origin}/ai/llama/health`)
  t.equal(res.status, 200, 'absolute socket URL health returns 200')
  const json = await res.json()
  t.equal(json.status, 'ok', 'status ok via absolute URL')
})

test('llama-server: models list shape', async (t) => {
  const res = await fetch('/ai/llama/v1/models')
  t.equal(res.status, 200, 'models returns 200')
  const json = await res.json()
  t.equal(json.object, 'list', 'object is list')
  t.ok(Array.isArray(json.data), 'data is array')
})

test('llama-server: embeddings without model', async (t) => {
  const res = await fetch('/ai/llama/v1/embeddings?input=hello')
  const text = await res.text()
  // without a model, server should respond 400 with error JSON
  t.equal(res.status, 400, 'embeddings returns 400 without model')
  t.ok(/No model loaded/.test(text), 'error message mentions no model loaded')
})

test('llama-server: chat non-stream without model', async (t) => {
  const res = await fetch('/ai/llama/v1/chat/completions?prompt=hello')
  const text = await res.text()
  t.equal(res.status, 400, 'chat returns 400 without model')
  t.ok(/No model loaded/.test(text), 'error message mentions no model loaded')
})

test('llama-server: chat SSE via query param without model', async (t) => {
  const url = '/ai/llama/v1/chat/completions?prompt=hello&stream=true'
  const controller = new AbortController()
  const res = await fetch(url, { signal: controller.signal })
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
    // Expect server to finish quickly with an error or DONE without a model
    if (chunks.includes('[DONE]')) break
    if (chunks.length > 4096) break // safety
  }
  // Expect at least one SSE payload
  t.ok(/data:/.test(chunks), 'received at least one SSE event')
})

test('llama-server: chat SSE via JSON body without model', async (t) => {
  const url = '/ai/llama/v1/chat/completions'
  const body = JSON.stringify({
    stream: true,
    messages: [{ role: 'user', content: 'hello' }]
  })
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

test('llama-server: prompt too large returns 413', async (t) => {
  const big = 'x'.repeat(150 * 1024) // 150 KiB
  const res = await fetch('/ai/llama/v1/chat/completions', {
    method: 'POST',
    body: big
  })
  t.equal(res.status, 413, 'payload rejected (prompt too large)')
})
