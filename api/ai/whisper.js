/**
 * @module ai/whisper
 *
 * High-level helpers for invoking the embedded `whisper.cpp` speech-to-text
 * runtime. Load a `WhisperModel`, then call {@link WhisperModel#transcribe}
 * with PCM audio (Float32Array or Int16Array). The runtime automatically
 * handles multi-channel downmixing, resampling to 16 kHz, optional
 * normalization, and can stream partial hypotheses via `onSegment`.
 */
import { Buffer } from '../buffer.js'
import { rand64 } from '../crypto.js'
import ipc from '../ipc.js'

const SAMPLE_RATE = 16000

let conduitCtorPromise = null
async function loadConduit () {
  if (!conduitCtorPromise) {
    conduitCtorPromise = import('../conduit.js').then((mod) => mod.default)
  }
  return conduitCtorPromise
}

function toArrayBuffer (view) {
  if (view.byteOffset === 0 && view.byteLength === view.buffer.byteLength) {
    return view.buffer
  }
  return view.buffer.slice(view.byteOffset, view.byteOffset + view.byteLength)
}

function normalizeAudio (input, opts = {}) {
  const options = opts || {}
  const sampleRate = options.sampleRate ?? SAMPLE_RATE
  const channels = Math.max(1, parseInt(options.channels ?? 1, 10) || 1)

  if (ArrayBuffer.isView(input)) {
    if (input instanceof Float32Array) {
      return {
        buffer: toArrayBuffer(input),
        format: 'f32',
        sampleRate,
        channels
      }
    }

    if (input instanceof Int16Array) {
      return {
        buffer: toArrayBuffer(input),
        format: 'pcm16',
        sampleRate,
        channels
      }
    }

    throw new TypeError('Unsupported typed array for whisper audio input')
  }

  if (input instanceof ArrayBuffer) {
    return {
      buffer: input,
      format: options.format || 'f32',
      sampleRate,
      channels
    }
  }

  throw new TypeError(
    'Audio input must be an ArrayBuffer, Float32Array, or Int16Array'
  )
}

function toBooleanFlag (value, defaultValue = false) {
  if (typeof value === 'undefined' || value === null) {
    return defaultValue ? 'true' : ''
  }

  return value ? 'true' : ''
}

/**
 * Speech-to-text model backed by `whisper.cpp`.
 *
 * ```js
 * import whisper from 'oro:ai/whisper'
 *
 * const model = new whisper.WhisperModel({ name: 'ggml-base.en.bin' })
 * await model.load({ directory: '/path/to/models' })
 * const result = await model.transcribe(new Int16Array(audioBuffer), {
 *   sampleRate: 44100,
 *   channels: 2,
 *   normalize: true,
 *   onSegment (segment) {
 *     console.log('partial', segment.text)
 *   }
 * })
 * console.log(result.text)
 * ```
 */
export class WhisperModel {
  #id
  #name
  #loaded = false

  /**
   * @param {{ id?: number|null, name?: string|null }} [options]
   * Provide either a numeric `id` (returned from previous loads) or a model
   * `name` that exists on disk.
   */
  constructor ({ id = null, name = null } = {}) {
    if (!id && !name) {
      throw new TypeError('Expected whisper model id or name')
    }
    this.#id = id
    this.#name = name
  }

  get id () {
    return this.#id
  }

  get name () {
    return this.#name
  }

  get loaded () {
    return this.#loaded
  }

  /**
   * Loads the whisper model into memory (if not already loaded).
   *
   * @param {{ directory?: string, threadCount?: number, statePoolLimit?: number, useGPU?: boolean, gpuDevice?: number }} [options]
   * @return {Promise<any>} Resolves when the model is ready.
   */
  async load (options = {}) {
    const payload = {
      id: this.#id ?? '',
      name: this.#name ?? '',
      directory: options.directory ?? '',
      threadCount: options.threadCount ?? '',
      statePoolLimit: options.statePoolLimit ?? '',
      useGPU: options.useGPU ? 'true' : '',
      gpuDevice: options.gpuDevice ?? ''
    }

    const result = await ipc.request('ai.whisper.model.load', payload)
    if (result.err) {
      throw result.err
    }

    const data = result.data?.data ?? result.data
    if (data) {
      if (typeof data.id !== 'undefined') {
        const parsed = Number.parseInt(data.id, 10)
        if (!Number.isNaN(parsed)) {
          this.#id = parsed
        }
      }
      if (typeof data.name === 'string' && data.name.length > 0) {
        this.#name = data.name
      }
    }

    this.#loaded = true
    return data
  }

  /**
   * Unload the model from memory.
   * @return {Promise<any>}
   */
  async unload () {
    const result = await ipc.request('ai.whisper.model.unload', {
      id: this.#id ?? '',
      name: this.#name ?? ''
    })

    if (result.err) {
      throw result.err
    }

    this.#loaded = false
    return result.data?.data ?? result.data
  }

  /**
   * Transcribe PCM audio to text.
   *
   * @param {ArrayBufferView|ArrayBuffer} audio PCM samples (Float32Array or Int16Array recommended).
   * @param {import('../index.js').WhisperTranscribeOptions} [options]
   *   - `sampleRate`: source sample rate (defaults to 16 kHz).
   *   - `channels`: channel count (multi-channel buffers are averaged to mono).
   *   - `normalize`: scale waveform before inference.
   *   - `stream`: emit partial segments via `onSegment`.
   *   - `signal`: optional AbortSignal to cancel the request.
   *   - `enableVAD`: enable voice-activity detection (when supported by the runtime).
   *   - `vadModelPath`: optional path to a dedicated VAD model (GGUF) to use when `enableVAD` is true.
   * @return {Promise<any>} Resolves with transcription metadata.
   */
  async transcribe (audio, options = {}) {
    if (!audio) {
      throw new TypeError('Audio input is required')
    }

    if (!this.#loaded) {
      await this.load()
    }

    const { buffer, format, sampleRate, channels } = normalizeAudio(
      audio,
      options
    )

    const payload = {
      id: this.#id ?? '',
      name: this.#name ?? '',
      format,
      sampleRate,
      channels,
      language: options.language ?? '',
      translate: toBooleanFlag(options.translate),
      detectLanguage: toBooleanFlag(options.detectLanguage),
      timestamps: options.timestamps === false ? 'false' : 'true',
      wordTimestamps: toBooleanFlag(options.wordTimestamps),
      diarize: toBooleanFlag(options.diarize),
      threadCount: options.threadCount ?? '',
      maxSegmentLength: options.maxSegmentLength ?? '',
      temperature:
        typeof options.temperature === 'number' ? options.temperature : '',
      temperatureIncrement:
        typeof options.temperatureIncrement === 'number'
          ? options.temperatureIncrement
          : '',
      entropyThreshold:
        typeof options.entropyThreshold === 'number'
          ? options.entropyThreshold
          : '',
      logProbThreshold:
        typeof options.logProbThreshold === 'number'
          ? options.logProbThreshold
          : '',
      noSpeechThreshold:
        typeof options.noSpeechThreshold === 'number'
          ? options.noSpeechThreshold
          : '',
      normalize: options.normalize ? 'true' : '',
      enableVAD: toBooleanFlag(options.enableVAD),
      vadModelPath: options.vadModelPath ?? ''
    }

    const onSegment =
      typeof options.onSegment === 'function' ? options.onSegment : null
    if (!onSegment && options.stream === true) {
      throw new TypeError(
        'options.onSegment callback is required when stream is enabled'
      )
    }
    let conduit = options.conduit || null
    let shouldCloseConduit = false
    let conduitError = null

    if (onSegment) {
      if (!conduit) {
        const ConduitCtor = await loadConduit()
        conduit = new ConduitCtor({ id: String(rand64()) })
        shouldCloseConduit = true
      }
      const conduitId = conduit?.id ?? null
      if (!conduitId) {
        throw new Error(
          'Conduit instance must expose a stable id when supplying options.conduit'
        )
      }
      payload.stream = 'true'
      payload.conduit = conduitId

      conduit.receive((err, decoded) => {
        if (err) {
          conduitError = err
          return
        }
        if (!decoded || !decoded.options) return
        const source = decoded.options.source
        if (
          source !== 'ai.whisper.transcribe.stream' &&
          source !== 'ai.whisper.transcribe.complete'
        ) {
          return
        }

        const text = Buffer.from(decoded.payload).toString()
        const segment = {
          source,
          index: Number(
            decoded.options.index ??
              (source === 'ai.whisper.transcribe.complete' ? -1 : 0)
          ),
          start: Number(decoded.options.start ?? 0),
          end: Number(decoded.options.end ?? 0),
          confidence: Number(decoded.options.confidence ?? 0),
          language: decoded.options.language ?? null,
          text,
          done: Boolean(
            decoded.options.finished ||
              decoded.options.complete ||
              source === 'ai.whisper.transcribe.complete'
          )
        }

        // Optional alignment / VAD metadata when provided by the runtime
        if (typeof decoded.options.startDTW === 'number') {
          segment.startDTW = decoded.options.startDTW
        }
        if (Array.isArray(decoded.options.vadSegments)) {
          segment.vadSegments = decoded.options.vadSegments
        }

        onSegment(segment)
      })

      if (!conduit.isActive) {
        try {
          await conduit.connect()
        } catch (err) {
          conduitError = err
        }
      }
    }

    let result
    const sendOptions = { bytes: buffer }
    if (
      typeof globalThis.AbortSignal !== 'undefined' &&
      options.signal instanceof globalThis.AbortSignal
    ) {
      sendOptions.signal = options.signal
    }
    try {
      result = await ipc.send('ai.whisper.transcribe', payload, sendOptions)
    } finally {
      if (onSegment && shouldCloseConduit && conduit) {
        conduit.close()
      }
    }

    if (conduitError) {
      throw conduitError
    }

    if (result.err) {
      throw result.err
    }

    return result.data?.data ?? result.data
  }
}

export async function listModels () {
  const result = await ipc.request('ai.whisper.model.list')
  if (result.err) {
    throw result.err
  }
  return result.data?.data ?? result.data ?? []
}

export async function unloadModel (idOrName) {
  if (!idOrName) {
    throw new TypeError('Model identifier is required')
  }

  const payload =
    typeof idOrName === 'number' ? { id: idOrName } : { name: idOrName }

  const result = await ipc.request('ai.whisper.model.unload', payload)
  if (result.err) {
    throw result.err
  }
  return result.data?.data ?? result.data
}

export default {
  WhisperModel,
  listModels,
  unloadModel
}
