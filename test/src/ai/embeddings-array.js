import test from 'oro:test'
import process from 'oro:process'

test('llama-server: embeddings array input shape (conditional)', async (t) => {
  const name = process.env.ORO_AI_MODEL_NAME
  if (!name) {
    return t.comment('ORO_AI_MODEL_NAME not set — skipping embeddings test')
  }

  // Try to load model (ignore if already loaded)
  try {
    await fetch('ipc://ai.llm.model.load?name=' + encodeURIComponent(name))
  } catch {}

  // Small array should pass (200)
  const small = { input: ['hello', 'world'] }
  const res1 = await fetch('/ai/llama/v1/embeddings', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(small)
  })
  t.equal(
    res1.status,
    200,
    'embeddings small array returns 200 (if model loaded)'
  )

  // Large total input should trigger 413 (default cap 262144 bytes)
  const bigStr = 'a'.repeat(140 * 1024)
  const large = { input: [bigStr, bigStr] }
  const res2 = await fetch('/ai/llama/v1/embeddings', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(large)
  })
  t.equal(res2.status, 413, 'embeddings large array returns 413')
})
