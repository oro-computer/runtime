# Whisper Speech Integration

Oro Runtime vendors [`whisper.cpp`](https://github.com/ggerganov/whisper.cpp.git) for
per-device speech-to-text. This document summarizes build requirements, IPC
endpoints, and the JavaScript API.

> See [`README.md`](../README.md) for a quick-start example and microphone
> capture snippet.

## Build & Packaging

The install script builds whisper alongside libuv/llama when `./bin/install.sh`
(or `ORO_HOME`/`PREFIX` packaging) is invoked. `_compile_whisper` currently
supports:

- **Desktop** (`x86_64-desktop`) – default path, verified.
- **Windows** – produces `whisper.lib` (experimental; requires MS toolchain).
- **iOS** (`iPhoneOS`/`iPhoneSimulator`) – uses `xcrun` clang/bitcode.
- **Android** (NDK) – static `libwhisper.a` per ABI.

> Pending: run platform-specific builds in CI to confirm toolchains and linking.

## IPC Routes

The runtime exposes whisper control commands through `ipc://`:

| Command                   | Description                                                                            |
| ------------------------- | -------------------------------------------------------------------------------------- |
| `ai.whisper.model.load`   | Load a model into memory (by `name` or `id`, optional `directory`, threading options). |
| `ai.whisper.model.list`   | List loaded models.                                                                    |
| `ai.whisper.model.unload` | Unload a model by `id` or `name`.                                                      |
| `ai.whisper.transcribe`   | Transcribe PCM payload (`POST` body) with optional query parameters (see below).       |

### Transcribe Query Parameters

- `id` / `name` – identify the model (one required).
- `format` – `f32` (default) or `pcm16`.
- `sampleRate` – source sample rate (default 16000).
- `channels` – channel count (default 1).
- `normalize` – `true` to scale waveform prior to inference.
- `stream` – `true` to stream partial segments over Conduit.
- `language`, `translate`, `detectLanguage`, `timestamps`, `wordTimestamps`, `diarize`, `threadCount`, `maxSegmentLength`, `temperature`, `temperatureIncrement`, `entropyThreshold`, `logProbThreshold`, `noSpeechThreshold` – forwarded to whisper.

When `stream=true` and a Conduit client exists for the `id`, segments are pushed
incrementally with payloads encoded via the Conduit binary framing. If no
Conduit client is present, partial segments are emitted via `ipc.write` queued
responses (sequence `-1`).

### Response Structure

```json
{
  "text": "full transcription",
  "language": "en",
  "audioMs": 1234.5,
  "processingMs": 456.7,
  "inputSampleRate": 44100,
  "inputSamples": 44100,
  "outputSamples": 16000,
  "resampled": true,
  "normalized": true,
  "segments": [
    {
      "index": 0,
      "start": 0.0,
      "end": 1.0,
      "text": "hello",
      "confidence": 0.95
    }
  ]
}
```

## JavaScript API (`oro:ai/whisper`)

```js
import whisper from 'oro:ai/whisper'

const model = new whisper.WhisperModel({ name: 'ggml-base.en.bin' })
await model.load({ directory: '/abs/path/to/models' })

const audio = new Int16Array(/* PCM16 samples */)
const result = await model.transcribe(audio, {
  sampleRate: 44100,
  channels: 2,
  normalize: true,
  stream: true,
  onSegment(segment) {
    console.log(segment)
  },
  signal: abortController.signal,
})
```

Options (typed in `api/index.d.ts`):

- `sampleRate`, `channels`, `format` – audio metadata.
- `normalize` – enable RMS normalization.
- `stream` + `onSegment` – receive partial hypotheses (requires Conduit).
- `signal` – abort the request.
- Other decoding knobs mirror llama (`temperature`, `maxSegmentLength`, etc.).

### Helpers

- `whisper.listModels()` – fetch loaded models.
- `whisper.unloadModel(idOrName)` – unload a specific model.

## Testing

The integration tests expect heavy fixtures and are skipped by default. Provide
the following environment variables to enable them:

```bash
export ORO_TEST_WHISPER_MODEL=/absolute/path/to/ggml-base.en.bin
export ORO_TEST_WHISPER_AUDIO=/absolute/path/to/audio.pcm
export ORO_TEST_WHISPER_AUDIO_FORMAT=pcm16   # optional
export ORO_TEST_WHISPER_AUDIO_SAMPLE_RATE=44100  # optional
export ORO_TEST_WHISPER_AUDIO_CHANNELS=2   # optional
```

Then run (optionally skipping native test extensions in CI):

```bash
ORO_TEST_SKIP_DESKTOP_EXTENSION=1 \
ORO_TEST_SKIP_TEST_EXTENSIONS=1 \
ORO_TEST_WHISPER_MODEL=... \
ORO_TEST_WHISPER_AUDIO=... \
npm test whisper-streaming -- --runInBand
```

## Configuration Keys

The following user-config (`oro.toml`) keys affect whisper:

- `ai_whisper_model_path` – directory fallback when loading models.
- `ai_whisper_queue_limit` – max queued transcription jobs (defaults to 4).

Per-model keys follow the same pattern as llama (e.g. `ai_whisper_model_<name>_*`).

## Notes & Limitations

- Non-16 kHz input is resampled via linear interpolation; consider higher-quality
  resamplers if your application demands it.
- Streaming currently emits full text for each partial segment; consumers may
  want to diff successive outputs.
- iOS/Android/Windows builds require platform toolchains; confirm in CI before
  shipping to end users.
