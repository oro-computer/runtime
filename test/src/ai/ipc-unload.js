import test from 'oro:test'

test('ipc: ai.llm.model.unload returns error when missing', async (t) => {
  const res = await fetch('ipc://ai.llm.model.unload?name=__does_not_exist__')
  t.equal(res.status, 200, 'IPC returns 200 envelope')
  const json = await res.json().catch(() => ({}))
  t.ok(
    json.err && /Model not found/i.test(json.err.message || ''),
    'err contains "Model not found"'
  )
})

test('ipc: ai.llm.lora.unload returns error when missing', async (t) => {
  const res = await fetch('ipc://ai.llm.lora.unload?id=9999999')
  t.equal(res.status, 200, 'IPC returns 200 envelope')
  const json = await res.json().catch(() => ({}))
  t.ok(
    json.err && /LoRA not found/i.test(json.err.message || ''),
    'err contains "LoRA not found"'
  )
})
