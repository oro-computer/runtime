import test, { after as afterAll } from 'oro:test'
import path from 'oro:path'
import fs from 'oro:fs/promises'
import whisper from 'oro:ai/whisper'

const modelEnv = process.env.ORO_TEST_WHISPER_MODEL
const audioEnv = process.env.ORO_TEST_WHISPER_AUDIO
const audioFormatEnv = process.env.ORO_TEST_WHISPER_AUDIO_FORMAT || 'pcm16'
const audioSampleRateEnv = Number.parseInt(
  process.env.ORO_TEST_WHISPER_AUDIO_SAMPLE_RATE || '16000',
  10
)
const audioChannelsEnv = Number.parseInt(
  process.env.ORO_TEST_WHISPER_AUDIO_CHANNELS || '1',
  10
)

let cachedModelPromise = null
let cachedModel = null

async function ensureModel () {
  if (!modelEnv || !audioEnv) {
    return null
  }
  if (!cachedModelPromise) {
    cachedModelPromise = (async () => {
      const dirname = path.dirname(modelEnv)
      const name = path.basename(modelEnv)
      const model = new whisper.WhisperModel({ name })
      await model.load({ directory: dirname })
      cachedModel = model
      return model
    })()
  }
  return cachedModelPromise
}

function bufferToTypedArray (buffer, format) {
  const slice = buffer.buffer.slice(
    buffer.byteOffset,
    buffer.byteOffset + buffer.byteLength
  )
  if (format === 'pcm16' || format === 's16') {
    return new Int16Array(slice)
  }
  return new Float32Array(slice)
}

afterAll(async () => {
  if (cachedModel) {
    try {
      await cachedModel.unload()
    } catch {}
    cachedModel = null
    cachedModelPromise = null
  }
})

async function readAudioFixture () {
  const buf = await fs.readFile(audioEnv)
  return bufferToTypedArray(buf, audioFormatEnv)
}

test('whisper: transcribe returns metadata', async (t) => {
  if (!modelEnv || !audioEnv) {
    t.skip(
      'set ORO_TEST_WHISPER_MODEL and ORO_TEST_WHISPER_AUDIO to enable whisper tests'
    )
    return
  }

  const model = await ensureModel()
  const audio = await readAudioFixture()
  const transcription = await model.transcribe(audio, {
    sampleRate: audioSampleRateEnv,
    channels: audioChannelsEnv,
    format: audioFormatEnv,
    normalize: true
  })

  t.ok(typeof transcription.text === 'string', 'produces text output')
  t.equal(
    typeof transcription.processingMs,
    'number',
    'processing time emitted'
  )
  t.equal(typeof transcription.audioMs, 'number', 'audio duration emitted')
  t.equal(
    transcription.inputSampleRate,
    audioSampleRateEnv,
    'reports original sample rate'
  )
  t.equal(transcription.inputSamples > 0, true, 'tracks input sample count')
  t.equal(Array.isArray(transcription.segments), true, 'segments array present')
})

test('whisper: streaming delivers segments and final completion', async (t) => {
  if (!modelEnv || !audioEnv) {
    t.skip(
      'set ORO_TEST_WHISPER_MODEL and ORO_TEST_WHISPER_AUDIO to enable whisper tests'
    )
    return
  }

  const model = await ensureModel()
  const audio = await readAudioFixture()
  const segments = []

  await model.transcribe(audio, {
    sampleRate: audioSampleRateEnv,
    channels: audioChannelsEnv,
    format: audioFormatEnv,
    stream: true,
    onSegment (segment) {
      segments.push(segment)
    }
  })

  t.ok(segments.length > 0, 'received streaming segments')
  t.ok(
    segments.some((segment) => segment.done === true),
    'received final completion segment'
  )
})

test('whisper: aborting transcription rejects with AbortError', async (t) => {
  if (!modelEnv || !audioEnv) {
    t.skip(
      'set ORO_TEST_WHISPER_MODEL and ORO_TEST_WHISPER_AUDIO to enable whisper tests'
    )
    return
  }

  const model = await ensureModel()
  const audio = await readAudioFixture()

  const controller = new AbortController()
  controller.abort()

  try {
    await model.transcribe(audio, {
      sampleRate: audioSampleRateEnv,
      channels: audioChannelsEnv,
      format: audioFormatEnv,
      signal: controller.signal
    })
    t.fail('expected transcribe to reject when aborted')
  } catch (err) {
    t.equal(
      err?.name,
      'AbortError',
      'transcribe rejects with AbortError when signal is aborted'
    )
  }
})
