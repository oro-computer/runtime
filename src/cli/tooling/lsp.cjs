'use strict'

const { Transform } = require('node:stream')

const maximumMessage = 16 * 1024 * 1024

/**
 * Encode a JSON-RPC message using LSP byte framing.
 * @param {object} message
 * @returns {Buffer}
 */
function encode (message) {
  const body = Buffer.from(JSON.stringify(message))
  return Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`), body])
}

/** A bounded, backpressure-aware LSP message transform. */
class Messages extends Transform {
  /**
   * @param {function(object): object[]} transform
   */
  constructor (transform) {
    super()
    this.transformMessage = transform
    this.pending = Buffer.alloc(0)
    this.length = null
  }

  _transform (chunk, encoding, callback) {
    try {
      this.pending = Buffer.concat([this.pending, chunk])
      for (;;) {
        if (this.length === null) {
          const end = this.pending.indexOf('\r\n\r\n')
          if (end < 0) {
            if (this.pending.length > 8192) throw new Error('LSP header exceeds 8 KiB')
            break
          }
          if (end > 8192) throw new Error('LSP header exceeds 8 KiB')
          const headers = this.pending.subarray(0, end).toString('ascii').split('\r\n')
          const lengths = headers.filter(header => /^content-length:/i.test(header))
          if (lengths.length !== 1 || !/^content-length:\s*\d+\s*$/i.test(lengths[0])) throw new Error('Invalid LSP Content-Length')
          this.length = Number(lengths[0].split(':')[1].trim())
          if (this.length < 1 || this.length > maximumMessage) throw new Error('LSP message exceeds 16 MiB')
          this.pending = this.pending.subarray(end + 4)
        }
        if (this.pending.length < this.length) break
        const message = JSON.parse(this.pending.subarray(0, this.length).toString('utf8'))
        this.pending = this.pending.subarray(this.length)
        this.length = null
        if (!message || typeof message !== 'object' || Array.isArray(message)) throw new Error('Invalid LSP message')
        for (const result of this.transformMessage(message)) this.push(encode(result))
      }
      callback()
    } catch (error) {
      callback(error)
    }
  }

  _flush (callback) {
    callback(this.pending.length || this.length !== null ? new Error('Truncated LSP message') : undefined)
  }
}

/**
 * @typedef {object} InitializedClient
 * @property {object} message The first initialize request, also retained in input.
 * @property {Messages} input Framed input including initialize and subsequent messages.
 * @property {function(): void} close Detach the source and release buffered input.
 */

/**
 * Read initialize before project discovery, retaining frames under backpressure
 * until the backend starts. No client messages are consumed or replayed twice.
 * @param {import('node:stream').Readable} source
 * @returns {Promise<InitializedClient>}
 */
async function initializeClient (source) {
  let message
  let accept
  let rejectInitialization
  const ready = new Promise((resolve, reject) => { accept = resolve; rejectInitialization = reject })
  const deadline = setTimeout(() => { input.destroy(new Error('Editor did not send initialize within 30 seconds')) }, 30000)
  const input = new Messages(request => {
    if (!message) {
      if (request.method !== 'initialize' || request.id == null || !request.params || typeof request.params !== 'object' || Array.isArray(request.params)) {
        throw new Error('Expected an LSP initialize request before starting Oro Language Server')
      }
      message = request
      accept()
    }
    return [request]
  })
  const fail = error => input.destroy(error)
  const disconnected = () => {
    if (!source.readableEnded) fail(new Error('Editor disconnected before completing its LSP input'))
  }
  const close = () => {
    source.unpipe(input)
    source.pause()
    source.removeListener('error', fail)
    source.removeListener('close', disconnected)
    input.destroy()
  }
  // Retain the error listener after readiness; adapters also check input.errored
  // in case the editor sends malformed data while project preparation is running.
  input.on('error', rejectInitialization)
  input.once('finish', () => {
    if (!message) rejectInitialization(new Error('Editor input ended before an LSP initialize request arrived'))
  })
  source.on('error', fail)
  source.once('close', disconnected)
  source.pipe(input)
  try {
    await ready
    clearTimeout(deadline)
    return { message, input, close }
  } catch (error) {
    clearTimeout(deadline)
    close()
    throw error
  }
}

module.exports = { Messages, encode, initializeClient }
