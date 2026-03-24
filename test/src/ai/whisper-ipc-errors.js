import test from 'oro:test'

test('ipc: ai.whisper.model.load requires identifier', async (t) => {
  const res = await fetch('ipc://ai.whisper.model.load')
  t.equal(res.status, 200)
  const json = await res.json().catch(() => ({}))
  t.ok(json.err, 'response contains err')
  t.match(json.err?.message || '', /identifier/i, 'error mentions identifier')
})

test('ipc: ai.whisper.transcribe requires audio', async (t) => {
  const res = await fetch('ipc://ai.whisper.transcribe')
  t.equal(res.status, 200)
  const json = await res.json().catch(() => ({}))
  t.ok(json.err, 'response contains err')
  t.match(json.err?.message || '', /audio/i, 'error mentions audio')
})

test('ipc: ai.whisper.transcribe accepts non-16k sample rate via resampling', async (t) => {
  const samples = new Int16Array(8000) // 0.5s at 16-bit PCM
  const res = await fetch(
    'ipc://ai.whisper.transcribe?name=test-model&sampleRate=8000&format=pcm16',
    {
      method: 'POST',
      body: Buffer.from(samples.buffer)
    }
  )
  t.equal(res.status, 200, 'IPC envelope ok')
  const json = await res.json().catch(() => ({}))
  t.ok(
    json.err && /Whisper model not loaded/i.test(json.err.message || ''),
    'resample path reached model check'
  )
})

test('ipc: ai.whisper.transcribe supports multi-channel input', async (t) => {
  const frames = 4000
  const channels = 2
  const samples = new Int16Array(frames * channels)
  const res = await fetch(
    `ipc://ai.whisper.transcribe?name=test-model&sampleRate=44100&format=pcm16&channels=${channels}`,
    {
      method: 'POST',
      body: Buffer.from(samples.buffer)
    }
  )
  t.equal(res.status, 200, 'IPC envelope ok')
  const json = await res.json().catch(() => ({}))
  t.ok(
    json.err && /Whisper model not loaded/i.test(json.err.message || ''),
    'multi-channel path reached model check'
  )
})
