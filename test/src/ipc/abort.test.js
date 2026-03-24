import { test } from 'oro:test'
import ipc from 'oro:ipc'

test('ipc.request abort cleans up and subsequent request works', async (t) => {
  const ac = new AbortController()
  const p = ipc.request('platform.primordials', null, {
    signal: ac.signal,
    timeout: 5000
  })
  ac.abort('test-abort')
  const res = await p

  t.ok(res instanceof ipc.Result, 'returns ipc.Result on abort')
  t.equal(res.err?.name, 'AbortError', 'abort yields AbortError')

  const ok = await ipc.request('platform.primordials')
  t.ok(ok instanceof ipc.Result, 'subsequent request returns ipc.Result')
  t.ok(!ok.err && typeof ok.data === 'object', 'subsequent request succeeds')
})
