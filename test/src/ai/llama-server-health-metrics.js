import test from 'oro:test'

test('llama-server: /health exposes metrics and pool stats', async (t) => {
  const res = await fetch('/ai/llama/health')
  t.equal(res.status, 200, 'health returns 200')
  const json = await res.json().catch(() => ({}))
  t.equal(json.status, 'ok', 'status ok')
  t.ok(json.metrics && typeof json.metrics === 'object', 'metrics present')
  t.ok(typeof json.metrics.inflight === 'number', 'inflight metric present')
  t.ok(typeof json.metrics.errors === 'number', 'errors metric present')
  t.ok(typeof json.metrics.tooLarge === 'number', 'tooLarge metric present')
  t.ok(typeof json.metrics.timeouts === 'number', 'timeouts metric present')
  t.ok(
    json.metrics.chat && json.metrics.completions,
    'chat/completions blocks present'
  )
  t.ok(
    json.metrics.embeddings &&
      typeof json.metrics.embeddings.count === 'number',
    'embeddings block present'
  )
  t.ok(
    json.metrics.aggregate &&
      typeof json.metrics.aggregate.inflight === 'number',
    'aggregate metrics block present'
  )
  t.ok(Array.isArray(json.metrics.windows), 'per-window metrics array present')
  t.ok(
    json.pool && typeof json.pool.contexts === 'number',
    'pool contexts present'
  )
  t.ok(typeof json.pool.created === 'number', 'pool created present')
  t.ok(typeof json.pool.reused === 'number', 'pool reused present')
  t.ok(typeof json.pool.dropped === 'number', 'pool dropped present')
})
