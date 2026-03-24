import test from 'oro:test'

function randomText (n) {
  const a = new Uint8Array(n)
  for (let i = 0; i < a.length; i++) a[i] = 97 + (i % 26)
  try {
    return new TextDecoder().decode(a)
  } catch {
    return 'x'.repeat(n)
  }
}

test('llama-server: /tokenize small input ok', async (t) => {
  const res = await fetch('/ai/llama/tokenize?text=hello')
  t.equal(res.status, 200, 'tokenize returns 200 for small input')
  const json = await res.json().catch(() => ({}))
  t.ok(Array.isArray(json.tokens), 'tokens array present')
})

test('llama-server: /tokenize large input 413', async (t) => {
  // Default maxPromptBytes is 128 KiB; exceed it
  const big = randomText(150 * 1024)
  const url = '/ai/llama/tokenize'
  const res = await fetch(url, { method: 'POST', body: big })
  t.equal(res.status, 413, 'tokenize rejects oversized input')
})
