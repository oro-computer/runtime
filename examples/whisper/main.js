import whisper from 'oro:ai/whisper'
import fs from 'oro:fs/promises'
import process from 'oro:process'

import React from '../ui/react.js'
import {
  mountExample,
  ExampleLayout,
  ExamplePanel,
  ExampleStack,
  ExampleSection,
  ExampleProse,
  Button,
  Input,
  Select,
  Option,
  Badge,
  FormField,
  StatusPill,
  EmptyState,
  Switch,
  Checkbox,
  StatusBanner,
  LogViewer,
  cn
} from '../ui/index.js'

const {
  createElement: h,
  useState,
  useEffect,
  useMemo,
  useCallback,
  useRef
} = React

const dirStorageKey = 'whisper:model-dir'
const nameStorageKey = 'whisper:model-name'
const modeStorageKey = 'whisper:mode'

const hasStorage =
  typeof globalThis !== 'undefined' &&
  typeof globalThis.localStorage !== 'undefined'

function readStorage (key) {
  if (!hasStorage) return null
  try {
    return globalThis.localStorage.getItem(key)
  } catch {
    return null
  }
}

function writeStorage (key, value) {
  if (!hasStorage) return
  try {
    if (value === null || value === undefined || value === '') {
      globalThis.localStorage.removeItem(key)
    } else {
      globalThis.localStorage.setItem(key, value)
    }
  } catch {}
}

function expandHome (input) {
  if (!input || input[0] !== '~') return input
  const home = process?.env?.HOME || process?.env?.USERPROFILE || ''
  return home ? `${home}${input.slice(1)}` : input
}

function defaultModelDir () {
  const home = process?.env?.HOME || process?.env?.USERPROFILE
  if (home) return `${home}/models`
  return './models'
}

function formatMs (value) {
  const ms = Number(value) || 0
  if (ms < 1000) return `${Math.round(ms)} ms`
  const seconds = ms / 1000
  if (seconds < 60) return `${seconds.toFixed(1)} s`
  const minutes = Math.floor(seconds / 60)
  const rem = seconds - minutes * 60
  if (minutes >= 60) {
    const hours = Math.floor(minutes / 60)
    const remMinutes = minutes - hours * 60
    return `${hours}h ${remMinutes}m`
  }
  return `${minutes}m ${rem.toFixed(1)}s`
}

function formatDurationFromSamples (frames, sampleRate) {
  const rate = Number(sampleRate) || 16000
  const samples = Number(frames) || 0
  if (!samples || !rate) return '—'
  const seconds = samples / rate
  const minutes = Math.floor(seconds / 60)
  const secs = seconds - minutes * 60
  if (minutes === 0) return `${secs.toFixed(2)} s`
  return `${minutes}m ${secs.toFixed(1)}s`
}

function formatTimecode (seconds) {
  const value = Number(seconds)
  if (!Number.isFinite(value) || value < 0) return '0:00.000'
  const whole = Math.floor(value)
  const ms = Math.round((value - whole) * 1000)
  const mins = Math.floor(whole / 60)
  const secs = whole - mins * 60
  const pad = (n, len) => String(n).padStart(len, '0')
  return `${mins}:${pad(secs, 2)}.${pad(ms, 3)}`
}

function formatTimeRange (start, end) {
  const s = formatTimecode(start)
  const e = formatTimecode(end)
  return `${s} → ${e}`
}

function formatSampleRate (value) {
  const rate = Number(value) || 0
  if (!rate) return '—'
  if (rate >= 1000) return `${(rate / 1000).toFixed(1)} kHz`
  return `${rate} Hz`
}

function isModelFilename (name) {
  if (!name || typeof name !== 'string') return false
  const lower = name.toLowerCase()
  return lower.endsWith('.bin') || lower.endsWith('.gguf')
}

function createAudioContext () {
  const Ctor = globalThis.AudioContext || globalThis.webkitAudioContext
  if (!Ctor) {
    throw new Error('Web Audio API is not available in this environment.')
  }
  return new Ctor()
}

function describeStatusVariant (tone) {
  switch (tone) {
    case 'success':
      return 'success'
    case 'warning':
      return 'warning'
    case 'danger':
      return 'danger'
    case 'error':
      return 'danger'
    default:
      return 'info'
  }
}

function WhisperApp () {
  const [modelDir, setModelDir] = useState(
    () => readStorage(dirStorageKey) || defaultModelDir()
  )
  const [modelName, setModelName] = useState(
    () => readStorage(nameStorageKey) || 'ggml-base.en.bin'
  )
  const [mode, setMode] = useState(() => {
    const stored = readStorage(modeStorageKey)
    return stored === 'simple' || stored === 'stream' ? stored : 'stream'
  })

  const [status, setStatus] = useState(() => ({
    variant: 'info',
    title: 'Welcome to the Whisper studio',
    message:
      'Point to your model weights, drop in audio, and compare streaming versus full transcripts.'
  }))

  const [modelInfo, setModelInfo] = useState(null)
  const [models, setModels] = useState([])
  const [audioInfo, setAudioInfo] = useState(null)
  const [segments, setSegments] = useState([])
  const [transcript, setTranscript] = useState(null)

  const [isMicSupported] = useState(
    () =>
      typeof navigator !== 'undefined' &&
      !!navigator.mediaDevices &&
      !!navigator.mediaDevices.getUserMedia
  )
  const [isMicRecording, setIsMicRecording] = useState(false)
  const [micLevel, setMicLevel] = useState(0)

  const [normalize, setNormalize] = useState(true)
  const [detectLanguage, setDetectLanguage] = useState(true)
  const [translate, setTranslate] = useState(false)
  const [wordTimestamps, setWordTimestamps] = useState(false)
  const [enableVAD, setEnableVAD] = useState(false)
  const [vadModelPath, setVadModelPath] = useState('')
  const [languageHint, setLanguageHint] = useState('')

  const [isLoadingModel, setIsLoadingModel] = useState(false)
  const [isTranscribing, setIsTranscribing] = useState(false)

  const modelRef = useRef(null)
  const audioRef = useRef(null)
  const abortRef = useRef(null)
  const audioContextRef = useRef(null)
  const micStreamRef = useRef(null)
  const micSourceRef = useRef(null)
  const micProcessorRef = useRef(null)
  const micBufferRef = useRef(null)
  const micSampleRateRef = useRef(16000)

  const expandedModelDir = useMemo(() => {
    const value = (modelDir || '').trim()
    if (!value) return ''
    return expandHome(value)
  }, [modelDir])

  useEffect(() => {
    writeStorage(dirStorageKey, modelDir && modelDir.trim())
  }, [modelDir])

  useEffect(() => {
    writeStorage(nameStorageKey, modelName && modelName.trim())
  }, [modelName])

  useEffect(() => {
    writeStorage(modeStorageKey, mode)
  }, [mode])

  useEffect(() => {
    return () => {
      const current = modelRef.current
      modelRef.current = null
      if (current && typeof current.unload === 'function') {
        Promise.resolve(current.unload()).catch(() => {})
      }
      const ctx = audioContextRef.current
      audioContextRef.current = null
      if (ctx && typeof ctx.close === 'function') {
        ctx.close().catch(() => {})
      }

      const stream = micStreamRef.current
      micStreamRef.current = null
      if (stream) {
        stream.getTracks().forEach((track) => track.stop())
      }
      const proc = micProcessorRef.current
      micProcessorRef.current = null
      if (proc && typeof proc.disconnect === 'function') {
        proc.disconnect()
      }
      const src = micSourceRef.current
      micSourceRef.current = null
      if (src && typeof src.disconnect === 'function') {
        src.disconnect()
      }
    }
  }, [])

  const ensureAudioContext = useCallback(() => {
    if (audioContextRef.current) return audioContextRef.current
    const ctx = createAudioContext()
    audioContextRef.current = ctx
    return ctx
  }, [])

  const stopMic = useCallback(() => {
    setIsMicRecording(false)
    setMicLevel(0)
    const stream = micStreamRef.current
    micStreamRef.current = null
    if (stream) {
      stream.getTracks().forEach((track) => track.stop())
    }
    const proc = micProcessorRef.current
    micProcessorRef.current = null
    if (proc && typeof proc.disconnect === 'function') {
      proc.disconnect()
    }
    const src = micSourceRef.current
    micSourceRef.current = null
    if (src && typeof src.disconnect === 'function') {
      src.disconnect()
    }
  }, [])

  const handleToggleMic = useCallback(async () => {
    if (!isMicSupported) {
      setStatus({
        variant: 'warning',
        title: 'Microphone not available',
        message:
          'The current environment does not expose navigator.mediaDevices.getUserMedia.'
      })
      return
    }

    if (isMicRecording) {
      stopMic()
      return
    }

    setStatus({
      variant: 'info',
      title: 'Requesting microphone access…',
      message: 'Grant access to start capturing live audio.'
    })

    try {
      const ctx = ensureAudioContext()
      const stream = await navigator.mediaDevices.getUserMedia({
        audio: {
          channelCount: 1,
          echoCancellation: false,
          noiseSuppression: false,
          autoGainControl: false
        }
      })

      const source = ctx.createMediaStreamSource(stream)
      const processor = ctx.createScriptProcessor(4096, 1, 1)

      micStreamRef.current = stream
      micSourceRef.current = source
      micProcessorRef.current = processor
      micBufferRef.current = new Float32Array(0)
      micSampleRateRef.current = ctx.sampleRate || 16000

      processor.onaudioprocess = (event) => {
        const input = event.inputBuffer.getChannelData(0)
        if (!input || input.length === 0) return

        const prev = micBufferRef.current || new Float32Array(0)
        const combined = new Float32Array(prev.length + input.length)
        combined.set(prev, 0)
        combined.set(input, prev.length)
        micBufferRef.current = combined

        let peak = 0
        for (let i = 0; i < input.length; i++) {
          const value = Math.abs(input[i])
          if (value > peak) peak = value
        }
        setMicLevel(Math.min(1, peak * 4))
      }

      source.connect(processor)
      processor.connect(ctx.destination)

      setIsMicRecording(true)
      setStatus({
        variant: 'success',
        title: 'Microphone recording',
        message:
          'Speak into your microphone, then stop recording and run a transcription.'
      })
    } catch (err) {
      console.warn('whisper-example: microphone access failed', err)
      stopMic()
      setStatus({
        variant: 'danger',
        title: 'Microphone error',
        message: err?.message || String(err)
      })
    }
  }, [ensureAudioContext, isMicRecording, isMicSupported, stopMic])

  const handleUseMicBuffer = useCallback(() => {
    const buf = micBufferRef.current
    const sampleRate = micSampleRateRef.current || 16000
    if (!buf || buf.length === 0) {
      setStatus({
        variant: 'warning',
        title: 'No microphone audio',
        message:
          'Start recording and speak before capturing microphone audio into the buffer.'
      })
      return
    }

    const frames = buf.length
    const durationMs = (frames / sampleRate) * 1000

    audioRef.current = {
      pcm: buf.slice(0),
      sampleRate,
      channels: 1,
      frames,
      durationMs
    }

    setAudioInfo({
      fileName: 'Microphone capture',
      frames,
      sampleRate,
      channels: 1,
      durationMs
    })

    setStatus({
      variant: 'success',
      title: 'Microphone audio captured',
      message: `Recorded approximately ${formatDurationFromSamples(frames, sampleRate)} of live audio.`
    })
  }, [])

  const handleAudioFileChange = useCallback(
    async (event) => {
      const file = event.target.files && event.target.files[0]
      if (!file) {
        audioRef.current = null
        setAudioInfo(null)
        return
      }

      setStatus({
        variant: 'info',
        title: 'Decoding audio…',
        message: `Reading ${file.name} with the Web Audio API.`
      })

      try {
        const ctx = ensureAudioContext()
        const buffer = await file.arrayBuffer()
        const audioBuffer = await ctx.decodeAudioData(buffer.slice(0))
        const channels = audioBuffer.numberOfChannels || 1
        const frames = audioBuffer.length
        const sampleRate = audioBuffer.sampleRate || 16000

        let mono
        if (channels === 1) {
          mono = audioBuffer.getChannelData(0).slice(0)
        } else {
          mono = new Float32Array(frames)
          for (let ch = 0; ch < channels; ch++) {
            const data = audioBuffer.getChannelData(ch)
            for (let i = 0; i < frames; i++) {
              mono[i] += data[i] / channels
            }
          }
        }

        const durationMs = (frames / sampleRate) * 1000
        const info = {
          fileName: file.name,
          frames,
          sampleRate,
          channels: 1,
          durationMs
        }

        audioRef.current = {
          pcm: mono,
          sampleRate,
          channels: 1,
          frames,
          durationMs
        }

        setAudioInfo(info)
        setStatus({
          variant: 'success',
          title: 'Audio ready',
          message: `Decoded ${file.name} (${formatDurationFromSamples(frames, sampleRate)}).`
        })
      } catch (err) {
        console.warn('whisper-example: failed to decode audio', err)
        audioRef.current = null
        setAudioInfo(null)
        setStatus({
          variant: 'danger',
          title: 'Audio decode failed',
          message: err?.message || String(err)
        })
      }
    },
    [ensureAudioContext]
  )

  const refreshModels = useCallback(async () => {
    const directory = (modelDir || '').trim()
    if (!directory) {
      setModels([])
      return
    }

    const expandedDir = expandHome(directory)

    try {
      const entries = await fs.readdir(expandedDir)
      const candidates = Array.isArray(entries)
        ? entries.filter((name) => isModelFilename(name))
        : []

      setModels(candidates)

      if (!candidates.length) {
        setStatus({
          variant: 'warning',
          title: 'No models found',
          message: `No .bin or .gguf files were discovered under ${expandedDir}. You can still enter a filename manually and let the runtime use its configured search paths.`
        })
        return
      }

      if (!modelName || !candidates.includes(modelName)) {
        setModelName(candidates[0])
      }

      setStatus({
        variant: 'success',
        title: 'Models discovered',
        message: `Found ${candidates.length} model file${candidates.length === 1 ? '' : 's'} in ${expandedDir}.`
      })
    } catch (err) {
      console.warn('whisper-example: failed to list models in directory', err)
      setModels([])
      setStatus({
        variant: 'danger',
        title: 'Failed to read model directory',
        message: err?.message || String(err)
      })
    }
  }, [modelDir, modelName])

  const handleLoadModel = useCallback(async () => {
    const name = (modelName || '').trim()
    if (!name) {
      setStatus({
        variant: 'warning',
        title: 'Model name required',
        message:
          'Enter the filename of your Whisper model (for example, ggml-base.en.bin).'
      })
      return
    }

    const directory = (modelDir || '').trim()
    const expandedDir = expandHome(directory)

    setIsLoadingModel(true)
    setStatus({
      variant: 'info',
      title: 'Loading model…',
      message: expandedDir
        ? `Resolving "${name}" under ${expandedDir}.`
        : `Resolving "${name}" using the runtime search paths.`
    })

    const previous = modelRef.current
    modelRef.current = null
    if (previous && typeof previous.unload === 'function') {
      try {
        await previous.unload()
      } catch (err) {
        console.warn('whisper-example: failed to unload previous model', err)
      }
    }

    try {
      const model = new whisper.WhisperModel({ name })
      modelRef.current = model
      const options = expandedDir ? { directory: expandedDir } : {}
      const data = await model.load(options)

      const info = {
        id: model.id,
        name: model.name,
        filename: data?.filename || data?.path || '',
        loaded: model.loaded === true,
        options: data?.options || {}
      }
      setModelInfo(info)
      setStatus({
        variant: 'success',
        title: 'Model ready',
        message: `Model "${model.name}" loaded and ready to transcribe.`
      })
    } catch (err) {
      console.warn('whisper-example: model load failed', err)
      modelRef.current = null
      setModelInfo(null)
      setStatus({
        variant: 'danger',
        title: 'Model load failed',
        message: err?.message || String(err)
      })
    } finally {
      setIsLoadingModel(false)
    }
  }, [modelDir, modelName])

  const handleUnloadModel = useCallback(async () => {
    const current = modelRef.current
    if (!current) {
      setModelInfo(null)
      setStatus({
        variant: 'info',
        title: 'No model attached',
        message: 'Load a model before attempting to unload it.'
      })
      return
    }
    modelRef.current = null
    setIsLoadingModel(true)
    setStatus({
      variant: 'info',
      title: 'Unloading model…',
      message: 'Releasing Whisper weights from memory.'
    })
    try {
      await current.unload()
      setModelInfo(null)
      setStatus({
        variant: 'success',
        title: 'Model unloaded',
        message: 'The active Whisper model has been released.'
      })
    } catch (err) {
      console.warn('whisper-example: model unload failed', err)
      setStatus({
        variant: 'warning',
        title: 'Model unload failed',
        message: err?.message || String(err)
      })
    } finally {
      setIsLoadingModel(false)
    }
  }, [])

  const handleCancel = useCallback(() => {
    const controller = abortRef.current
    if (controller && typeof controller.abort === 'function') {
      controller.abort()
    }
  }, [])

  useEffect(() => {
    refreshModels().catch((err) => {
      console.warn('whisper-example: failed to refresh models', err)
    })
  }, [refreshModels])

  const handleTranscribe = useCallback(async () => {
    if (isTranscribing) return

    const audio = audioRef.current
    if (!audio || !audio.pcm || !audio.frames) {
      setStatus({
        variant: 'warning',
        title: 'Audio required',
        message:
          'Select an audio file and wait for it to finish decoding before starting transcription.'
      })
      return
    }

    const name = (modelName || '').trim()
    if (!name) {
      setStatus({
        variant: 'warning',
        title: 'Model name required',
        message:
          'Enter the filename of your Whisper model before transcribing.'
      })
      return
    }

    let model = modelRef.current
    if (!model || model.loaded !== true) {
      await handleLoadModel()
      model = modelRef.current
      if (!model || model.loaded !== true) {
        // handleLoadModel already surfaced the error status
        return
      }
    }

    setSegments([])
    setTranscript(null)

    const controller =
      typeof AbortController !== 'undefined' ? new AbortController() : null
    abortRef.current = controller

    const streaming = mode === 'stream'
    const startedAt =
      typeof performance !== 'undefined' && performance.now
        ? performance.now()
        : Date.now()

    setIsTranscribing(true)
    setStatus({
      variant: 'info',
      title: streaming ? 'Streaming transcription…' : 'Transcribing audio…',
      message: streaming
        ? 'Partial segments will appear in the left panel as they are decoded.'
        : 'Waiting for the full transcript from the runtime.'
    })

    try {
      const options = {
        sampleRate: audio.sampleRate,
        channels: audio.channels,
        normalize,
        stream: streaming,
        language: languageHint || undefined,
        detectLanguage,
        translate,
        wordTimestamps,
        enableVAD,
        vadModelPath:
          enableVAD && vadModelPath.trim() ? vadModelPath.trim() : undefined
      }

      if (controller && typeof controller.signal !== 'undefined') {
        options.signal = controller.signal
      }

      if (streaming) {
        options.onSegment = (segment) => {
          const entry = {
            id: `${segment.source}-${segment.index}-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`,
            text: segment.text || '',
            done: Boolean(segment.done),
            source: segment.source || 'ai.whisper.transcribe.stream',
            start: Number(segment.start ?? 0),
            end: Number(segment.end ?? 0),
            confidence:
              typeof segment.confidence === 'number'
                ? segment.confidence
                : null,
            language: segment.language || null
          }
          setSegments((prev) => [...prev, entry])
        }
      }

      const result = await model.transcribe(audio.pcm, options)

      const endedAt =
        typeof performance !== 'undefined' && performance.now
          ? performance.now()
          : Date.now()
      const wallMs = endedAt - startedAt

      const mergedSegments = Array.isArray(result.segments)
        ? result.segments
        : segments

      setTranscript({
        text: result.text || '',
        language: result.language || null,
        processingMs: result.processingMs || null,
        audioMs: result.audioMs || audio.durationMs,
        inputSampleRate: result.inputSampleRate || audio.sampleRate,
        inputSamples: result.inputSamples || audio.frames,
        wallMs,
        segmentsCount: Array.isArray(mergedSegments)
          ? mergedSegments.length
          : 0
      })

      setStatus({
        variant: 'success',
        title: 'Transcription complete',
        message: `Decoded ${Array.isArray(mergedSegments) ? mergedSegments.length : 1} segment${Array.isArray(mergedSegments) && mergedSegments.length === 1 ? '' : 's'} in ${formatMs(wallMs || result.processingMs)}.`
      })
    } catch (err) {
      if (err && err.name === 'AbortError') {
        setStatus({
          variant: 'warning',
          title: 'Transcription cancelled',
          message: 'The current transcription was aborted.'
        })
      } else {
        console.warn('whisper-example: transcription failed', err)
        setStatus({
          variant: 'danger',
          title: 'Transcription failed',
          message: err?.message || String(err)
        })
      }
    } finally {
      setIsTranscribing(false)
      abortRef.current = null
    }
  }, [
    handleLoadModel,
    isTranscribing,
    mode,
    normalize,
    detectLanguage,
    translate,
    wordTimestamps,
    enableVAD,
    vadModelPath,
    languageHint,
    modelName,
    segments
  ])

  const statusBanner = useMemo(() => {
    if (!status) return null
    return h(StatusBanner, {
      variant: describeStatusVariant(status.variant),
      title: status.title,
      message: status.message
    })
  }, [status])

  const segmentEntries = useMemo(() => segments, [segments])

  const renderSegmentEntry = useCallback((entry) => {
    if (!entry) return null
    const badgeVariant = entry.done ? 'success' : 'info'
    const badgeLabel = entry.done ? 'Final segment' : 'Partial segment'
    const timeLabel = formatTimeRange(entry.start, entry.end)
    const conf =
      typeof entry.confidence === 'number' && Number.isFinite(entry.confidence)
        ? `${(entry.confidence * 100).toFixed(1)}%`
        : null

    return h(
      'div',
      { className: cn('whisper__segment', entry.done && 'is-final') },
      h(
        'div',
        { className: 'whisper__segment-meta' },
        h(Badge, { variant: badgeVariant }, badgeLabel),
        h('span', { className: 'whisper__segment-time' }, timeLabel),
        conf && h('span', { className: 'whisper__segment-confidence' }, conf),
        entry.language &&
          h('span', { className: 'whisper__segment-language' }, entry.language)
      ),
      h(
        'div',
        { className: 'whisper__segment-text' },
        entry.text || '(empty segment)'
      )
    )
  }, [])

  const audioSummary = audioInfo
    ? h(
      'div',
      { className: 'whisper__audio-summary' },
      h(
        'div',
        { className: 'whisper__audio-summary-item' },
        h('div', { className: 'whisper__audio-summary-label' }, 'File'),
        h(
          'div',
          { className: 'whisper__audio-summary-value' },
          audioInfo.fileName || '—'
        )
      ),
      h(
        'div',
        { className: 'whisper__audio-summary-item' },
        h('div', { className: 'whisper__audio-summary-label' }, 'Duration'),
        h(
          'div',
          { className: 'whisper__audio-summary-value' },
          formatDurationFromSamples(audioInfo.frames, audioInfo.sampleRate)
        )
      ),
      h(
        'div',
        { className: 'whisper__audio-summary-item' },
        h(
          'div',
          { className: 'whisper__audio-summary-label' },
          'Sample rate'
        ),
        h(
          'div',
          { className: 'whisper__audio-summary-value' },
          formatSampleRate(audioInfo.sampleRate)
        )
      ),
      h(
        'div',
        { className: 'whisper__audio-summary-item' },
        h('div', { className: 'whisper__audio-summary-label' }, 'Channels'),
        h(
          'div',
          { className: 'whisper__audio-summary-value' },
          'Mono (downmixed)'
        )
      )
    )
    : h(EmptyState, {
      title: 'No audio selected',
      description: 'Drop a short clip here to prepare it for Whisper.'
    })

  const metrics = transcript
    ? h(
      'div',
      { className: 'whisper__metrics' },
      h(
        'div',
        { className: 'whisper__metric' },
        h('div', { className: 'whisper__metric-label' }, 'Language'),
        h(
          'div',
          { className: 'whisper__metric-value' },
          transcript.language ||
              (detectLanguage ? 'Auto-detected' : 'Not specified')
        )
      ),
      h(
        'div',
        { className: 'whisper__metric' },
        h('div', { className: 'whisper__metric-label' }, 'Audio length'),
        h(
          'div',
          { className: 'whisper__metric-value' },
          formatDurationFromSamples(
            transcript.inputSamples,
            transcript.inputSampleRate
          )
        )
      ),
      h(
        'div',
        { className: 'whisper__metric' },
        h('div', { className: 'whisper__metric-label' }, 'Processing time'),
        h(
          'div',
          { className: 'whisper__metric-value' },
          transcript.processingMs
            ? formatMs(transcript.processingMs)
            : formatMs(transcript.wallMs)
        )
      ),
      h(
        'div',
        { className: 'whisper__metric' },
        h('div', { className: 'whisper__metric-label' }, 'Segments'),
        h(
          'div',
          { className: 'whisper__metric-value' },
          transcript.segmentsCount || segments.length || 0
        )
      )
    )
    : null

  const transcriptNode = transcript
    ? h(
      'div',
      { className: 'whisper__transcript' },
      transcript.text || '(no text produced)'
    )
    : h(EmptyState, {
      title: 'No transcript yet',
      description: 'Run a transcription to see the decoded text.',
      className: 'whisper__transcript'
    })

  const effectiveStatusPill = modelInfo
    ? h(
      StatusPill,
      { status: 'running' },
        `Model: ${modelInfo.name || modelName || 'unknown'}`
    )
    : h(StatusPill, { status: 'idle' }, 'No model loaded')

  const isBusy = isLoadingModel || isTranscribing

  const aside = h(
    ExampleProse,
    { className: 'whisper__aside' },
    h(
      'p',
      null,
      'This demo is designed as a studio for local speech-to-text experiments. It pairs the embedded ',
      h('span', { className: 'ui-inline-code' }, 'whisper.cpp'),
      ' runtime with a Web Audio front-end.'
    ),
    h(
      'ul',
      null,
      h(
        'li',
        null,
        'Use any short WAV/MP3/OGG clip that your browser can decode.'
      ),
      h(
        'li',
        null,
        'Place model weights under your models directory and provide the filename.'
      ),
      h(
        'li',
        null,
        'Toggle streaming to compare partial hypotheses against the final transcript.'
      ),
      h(
        'li',
        null,
        'Experiment with language hints, translation, and voice-activity detection.'
      )
    )
  )

  return h(
    ExampleLayout,
    {
      title: 'Whisper Transcription Studio',
      description:
        'Load a Whisper model, drop in audio, and inspect both streaming segments and final transcripts from the Oro Runtime.',
      badge: effectiveStatusPill,
      aside
    },
    h(
      ExampleStack,
      { gap: 'lg', className: 'whisper__layout' },
      statusBanner,
      h(
        ExamplePanel,
        {
          title: 'Model and audio setup',
          description:
            'Point to your Whisper weights, choose an audio clip, and configure decoding options.'
        },
        h(
          ExampleStack,
          { gap: 'md' },
          h(
            ExampleSection,
            {
              title: 'Model',
              description:
                'Provide a model filename and optional directory. The runtime falls back to configured search paths when the directory is omitted.'
            },
            h(
              ExampleStack,
              { gap: 'sm' },
              h(
                FormField,
                {
                  label: 'Model directory',
                  description:
                    'Directory containing your Whisper model weights (GGML/GGUF).',
                  hint: expandedModelDir
                    ? `Resolved path: ${expandedModelDir}`
                    : 'Leave blank to use the runtime search paths. Tilde-prefixes are expanded against HOME.'
                },
                h(Input, {
                  value: modelDir,
                  onChange: (event) => setModelDir(event.target.value),
                  placeholder: '~/models'
                })
              ),
              h(
                FormField,
                {
                  label: 'Model filename',
                  required: true,
                  description: 'For example, ggml-base.en.bin or tiny.en.gguf.'
                },
                h(
                  ExampleStack,
                  { gap: 'sm' },
                  h(Input, {
                    value: modelName,
                    onChange: (event) => setModelName(event.target.value),
                    placeholder: 'ggml-base.en.bin'
                  }),
                  models.length > 0 &&
                    h(
                      Select,
                      {
                        value: models.includes(modelName) ? modelName : '',
                        onChange: (event) => setModelName(event.target.value)
                      },
                      h(
                        Option,
                        { value: '' },
                        models.length
                          ? 'Choose discovered model…'
                          : 'No models detected'
                      ),
                      models.map((name) =>
                        h(Option, { key: name, value: name }, name)
                      )
                    )
                )
              ),
              h(
                'div',
                { className: 'whisper__controls-row' },
                h(
                  Button,
                  {
                    type: 'button',
                    variant: 'secondary',
                    size: 'sm',
                    onClick: handleLoadModel,
                    disabled: isLoadingModel
                  },
                  isLoadingModel ? 'Loading…' : 'Load model'
                ),
                h(
                  Button,
                  {
                    type: 'button',
                    variant: 'outline',
                    size: 'sm',
                    onClick: () => {
                      refreshModels().catch((err) => {
                        console.warn(
                          'whisper-example: failed to refresh models on demand',
                          err
                        )
                      })
                    },
                    disabled: isLoadingModel
                  },
                  'Scan directory'
                ),
                h(
                  Button,
                  {
                    type: 'button',
                    variant: 'ghost',
                    size: 'sm',
                    onClick: handleUnloadModel,
                    disabled: isLoadingModel
                  },
                  'Unload'
                ),
                modelInfo &&
                  h(
                    'span',
                    { className: 'whisper__status-meta' },
                    h(
                      'span',
                      null,
                      h('strong', null, 'Path:'),
                      ' ',
                      modelInfo.filename || 'resolved via runtime'
                    ),
                    h(
                      'span',
                      null,
                      h('strong', null, 'Threads:'),
                      ' ',
                      modelInfo.options?.threadCount || 'auto'
                    )
                  )
              )
            )
          ),
          h(
            ExampleSection,
            {
              title: 'Audio',
              description:
                'Select a short clip (typically ≤ 30 seconds) or capture from your microphone. The demo uses the Web Audio API to extract mono PCM samples for Whisper.'
            },
            h(
              ExampleStack,
              { gap: 'sm' },
              h(
                FormField,
                {
                  label: 'Audio file',
                  description:
                    'WAV, MP3, OGG, or any format your browser can decode.'
                },
                h(Input, {
                  type: 'file',
                  accept: 'audio/*',
                  onChange: handleAudioFileChange
                })
              ),
              h(
                FormField,
                {
                  label: 'Microphone',
                  description: isMicSupported
                    ? 'Capture a live snippet from your default microphone and use it as the transcription source.'
                    : 'Microphone capture is not available in this environment.'
                },
                h(
                  'div',
                  { className: 'whisper__mic-row' },
                  h(
                    Button,
                    {
                      type: 'button',
                      variant: 'outline',
                      size: 'sm',
                      onClick: handleToggleMic,
                      disabled: !isMicSupported
                    },
                    isMicRecording ? 'Stop recording' : 'Start recording'
                  ),
                  h(
                    Button,
                    {
                      type: 'button',
                      variant: 'ghost',
                      size: 'sm',
                      onClick: handleUseMicBuffer,
                      disabled: !isMicSupported
                    },
                    'Use microphone buffer'
                  ),
                  h(
                    'div',
                    { className: 'whisper__mic-meter' },
                    h('div', {
                      className: 'whisper__mic-meter-fill',
                      style: {
                        transform: `scaleX(${Math.max(0.05, micLevel || 0)})`
                      }
                    })
                  )
                )
              ),
              audioSummary
            )
          ),
          h(
            ExampleSection,
            {
              title: 'Decoding options',
              description:
                'Adjust streaming, language hints, and optional voice-activity detection.'
            },
            h(
              ExampleStack,
              { gap: 'sm' },
              h(
                'div',
                { className: 'whisper__controls-row' },
                h(Switch, {
                  checked: mode === 'stream',
                  onCheckedChange: (checked) =>
                    setMode(checked ? 'stream' : 'simple'),
                  label: 'Streaming mode'
                }),
                h(
                  'span',
                  { className: 'whisper__status-meta' },
                  mode === 'stream'
                    ? 'Stream partial hypotheses via onSegment while the model runs.'
                    : 'Request only the final transcript without streaming.'
                )
              ),
              h(
                'div',
                { className: 'whisper__checkbox-row' },
                h(
                  Checkbox,
                  {
                    checked: normalize,
                    onChange: (event) => setNormalize(event.target.checked)
                  },
                  'Normalize waveform'
                ),
                h(
                  Checkbox,
                  {
                    checked: detectLanguage,
                    onChange: (event) =>
                      setDetectLanguage(event.target.checked)
                  },
                  'Auto-detect language'
                ),
                h(
                  Checkbox,
                  {
                    checked: translate,
                    onChange: (event) => setTranslate(event.target.checked)
                  },
                  'Translate to English'
                ),
                h(
                  Checkbox,
                  {
                    checked: wordTimestamps,
                    onChange: (event) =>
                      setWordTimestamps(event.target.checked)
                  },
                  'Word-level timestamps'
                ),
                h(
                  Checkbox,
                  {
                    checked: enableVAD,
                    onChange: (event) => setEnableVAD(event.target.checked)
                  },
                  'Enable VAD'
                )
              ),
              h(
                FormField,
                {
                  label: 'Language hint',
                  description:
                    'Optional BCP-47 language code (for example, en, fr, es).'
                },
                h(Input, {
                  value: languageHint,
                  onChange: (event) => setLanguageHint(event.target.value),
                  placeholder: 'auto'
                })
              ),
              h(
                FormField,
                {
                  label: 'VAD model path',
                  description:
                    'Optional GGUF path used when voice-activity detection is enabled.'
                },
                h(Input, {
                  value: vadModelPath,
                  onChange: (event) => setVadModelPath(event.target.value),
                  placeholder: '~/models/vad-ggml-base.en.gguf',
                  disabled: !enableVAD
                })
              ),
              h(
                'div',
                { className: 'whisper__controls-row' },
                h(
                  Button,
                  {
                    type: 'button',
                    variant: 'primary',
                    onClick: handleTranscribe,
                    disabled: isBusy
                  },
                  isTranscribing ? 'Transcribing…' : 'Start transcription'
                ),
                h(
                  Button,
                  {
                    type: 'button',
                    variant: 'outline',
                    onClick: handleCancel,
                    disabled: !isTranscribing
                  },
                  'Cancel'
                ),
                h(
                  'span',
                  { className: 'whisper__status-meta' },
                  h(
                    'span',
                    null,
                    'Whisper models run fully locally inside the Oro Runtime.'
                  )
                )
              )
            )
          )
        )
      ),
      h(
        ExamplePanel,
        {
          title: 'Streaming segments',
          description:
            mode === 'stream'
              ? 'Inspect partial hypotheses and final completions as they stream from the runtime.'
              : 'Streaming is disabled. Enable it in the options above to see segments here.'
        },
        h(LogViewer, {
          entries: segmentEntries,
          renderEntry: renderSegmentEntry,
          emptyState: () =>
            h(EmptyState, {
              title:
                mode === 'stream' ? 'No segments yet' : 'Streaming disabled',
              description:
                mode === 'stream'
                  ? 'Start a streaming transcription to see segments arrive in real time.'
                  : 'Turn on streaming mode in the options to capture partial segments.',
              className: 'whisper__segment'
            }),
          className: 'whisper__segments'
        })
      ),
      h(
        ExamplePanel,
        {
          title: 'Final transcript',
          description:
            'Review the stitched transcript and key performance metrics.'
        },
        h(ExampleStack, { gap: 'md' }, transcriptNode, metrics)
      )
    )
  )
}

mountExample(WhisperApp)
