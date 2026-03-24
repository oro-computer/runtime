export function listModels(): Promise<any>;
export function unloadModel(idOrName: any): Promise<any>;
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
    /**
     * @param {{ id?: number|null, name?: string|null }} [options]
     * Provide either a numeric `id` (returned from previous loads) or a model
     * `name` that exists on disk.
     */
    constructor({ id, name }?: {
        id?: number | null;
        name?: string | null;
    });
    get id(): number;
    get name(): string;
    get loaded(): boolean;
    /**
     * Loads the whisper model into memory (if not already loaded).
     *
     * @param {{ directory?: string, threadCount?: number, statePoolLimit?: number, useGPU?: boolean, gpuDevice?: number }} [options]
     * @return {Promise<any>} Resolves when the model is ready.
     */
    load(options?: {
        directory?: string;
        threadCount?: number;
        statePoolLimit?: number;
        useGPU?: boolean;
        gpuDevice?: number;
    }): Promise<any>;
    /**
     * Unload the model from memory.
     * @return {Promise<any>}
     */
    unload(): Promise<any>;
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
    transcribe(audio: ArrayBufferView | ArrayBuffer, options?: any): Promise<any>;
    #private;
}
declare namespace _default {
    export { WhisperModel };
    export { listModels };
    export { unloadModel };
}
export default _default;
