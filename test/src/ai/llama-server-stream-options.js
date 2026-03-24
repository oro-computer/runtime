import test from 'oro:test'

function decodeText (uint8) {
  try {
    return new TextDecoder().decode(uint8)
  } catch {
    return ''
  }
}

test('llama-server: chat SSE with max_decode_ms param', async (t) => {
  const url =
    '/ai/llama/v1/chat/completions?prompt=hello&stream=true&max_decode_ms=5'
  const res = await fetch(url)
  t.equal(res.status, 200, 'SSE returns 200')
  t.ok(
    (res.headers.get('content-type') || '').includes('text/event-stream'),
    'content-type is event-stream'
  )
  const reader = res.body.getReader()
  let gotData = false
  let chunks = ''
  const start = Date.now()
  while (Date.now() - start < 3000) {
    // 3s safety
    const { done, value } = await reader.read()
    if (done) break
    const text = decodeText(value)
    chunks += text
    if (/^data: /m.test(text)) gotData = true
    if (chunks.includes('[DONE]')) break
  }
  t.ok(gotData, 'received SSE data frames')
})
