import { IllegalConstructor } from '../util.js'
import { Metric } from './metric.js'
import registry from './channels.js'
import process from '../process.js'

const dc = registry.group('window', [
  'XMLHttpRequest.open',
  'XMLHttpRequest.send',
  'Worker',
  'fetch',
  'fetch.start',
  'fetch.end',
  'requestAnimationFrame'
])

/**
 * @typedef {object} RequestAnimationFrameMetricOptions
 * @property {number} [sampleSize=60] Rolling sample capacity, from 1 to 3600.
 */

/** Measures distinct animation frame timestamps using a bounded rolling buffer. */
export class RequestAnimationFrameMetric extends Metric {
  #sampleCount = 0
  #totalDuration = 0
  #lastTimestamp = null
  #generation = 0
  #wrappedRequestAnimationFrame = null
  #onExit = () => this.destroy()

  /** @param {RequestAnimationFrameMetricOptions} [options] */
  constructor (options) {
    super()
    this.originalRequestAnimationFrame = null
    this.requestAnimationFrame = this.requestAnimationFrame.bind(this)
    this.sampleSize = options?.sampleSize ?? 60
    if (!Number.isInteger(this.sampleSize) || this.sampleSize < 1 || this.sampleSize > 3600) {
      throw new RangeError('sampleSize must be an integer between 1 and 3600')
    }
    this.sampleTick = 0 // max(0, sampleSize)
    this.channel = dc.channel('requestAnimationFrame')
    this.value = null
    this.now = 0
  }

  requestAnimationFrame (callback) {
    return this.#wrappedRequestAnimationFrame(callback)
  }

  init () {
    if (
      this.originalRequestAnimationFrame === null &&
      typeof globalThis.requestAnimationFrame === 'function'
    ) {
      this.samples = new Float64Array(this.sampleSize)
      this.sampleTick = 0
      this.#sampleCount = 0
      this.#totalDuration = 0
      this.#lastTimestamp = null
      const generation = ++this.#generation
      const schedule = globalThis.requestAnimationFrame
      this.originalRequestAnimationFrame = globalThis.requestAnimationFrame
      // Each installed wrapper captures its scheduler. An outer wrapper may
      // retain it after stop, so it must continue forwarding without sampling.
      this.#wrappedRequestAnimationFrame = callback => schedule.call(globalThis, (...args) => {
        if (generation === this.#generation) this.update(...args)
        // eslint-disable-next-line n/no-callback-literal
        callback(...args)
      })
      globalThis.requestAnimationFrame = this.#wrappedRequestAnimationFrame

      process.once('exit', this.#onExit)
    }
  }

  /** @param {number} timestamp Animation frame timestamp in milliseconds. */
  update (timestamp) {
    if (!this.samples || !Number.isFinite(timestamp) || timestamp < 0) {
      return
    }

    const then = this.#lastTimestamp
    // All callbacks for one frame share a timestamp and contribute one sample.
    if (then !== null && timestamp <= then) {
      return
    }

    this.#lastTimestamp = timestamp
    this.now = timestamp
    if (then === null) return

    const duration = timestamp - then
    if (this.#sampleCount === this.sampleSize) {
      this.#totalDuration -= 1000 / this.samples[this.sampleTick]
    } else {
      this.#sampleCount++
    }

    this.samples[this.sampleTick] = 1000 / duration
    this.#totalDuration += duration
    this.sampleTick = (this.sampleTick + 1) % this.sampleSize
    const metric = {
      rate: Math.round(1000 * this.#sampleCount / this.#totalDuration),
      samples: this.#sampleCount
    }
    this.value = metric
    this.channel?.publish(metric)
  }

  destroy () {
    this.#generation++
    this.channel.reset()
    process.removeListener('exit', this.#onExit)

    if (globalThis.requestAnimationFrame === this.#wrappedRequestAnimationFrame) {
      globalThis.requestAnimationFrame = this.originalRequestAnimationFrame
    }

    this.originalRequestAnimationFrame = null
    this.samples = null
    this.value = null
    this.#lastTimestamp = null
    this.#sampleCount = 0
    this.#totalDuration = 0
  }

  toJSON () {
    return {
      sampleSize: this.sampleSize,
      sampleTick: this.sampleTick,
      samples: Array.from(this.samples ?? []),
      rate: this.value?.rate ?? 0,
      now: this.now
    }
  }
}

export class FetchMetric extends Metric {
  constructor (_options) {
    super()
    this.originalFetch = null
    this.channel = dc.channel('fetch')
    this.fetch = this.fetch.bind(this)
  }

  async fetch (resource, options, extra) {
    const metric = { resource, options, extra }
    this.channel.channel('start').publish(metric)

    const args = [resource, options, extra]
    const response = await this.originalFetch.apply(globalThis, args)
    metric.response = response.clone()

    this.channel.channel('end').publish(metric)
    this.channel.publish(metric)
    return response
  }

  init () {
    if (!this.originalFetch) {
      this.originalFetch = globalThis.fetch
      globalThis.fetch = this.fetch
    }
  }

  destroy () {
    this.channel.reset()

    if (typeof this.originalFetch === 'function') {
      globalThis.fetch = this.originalFetch
    }

    this.originalFetch = null
  }
}

export class XMLHttpRequestMetric extends Metric {
  constructor (_options) {
    super()
    this.channel = dc.channel('XMLHttpRequest')
    this.patched = null
  }

  init () {
    const { channel } = this

    if (!this.patched) {
      const { open, send } = globalThis.XMLHttpRequest.prototype
      this.patched = { open, send }

      Object.assign(globalThis.XMLHttpRequest.prototype, {
        open (...args) {
          const [method, url, async] = args
          channel.channel('open').publish({ request: this, method, url, async })
          return open.call(this, ...args)
        },

        send (...args) {
          const [body] = args
          channel.channel('send').publish({ request: this, body })
          return send.call(this, ...args)
        }
      })
    }
  }

  destroy () {
    this.channel.reset()

    if (this.patched) {
      Object.assign(globalThis.XMLHttpRequest.prototype, this.patched)
    }

    this.patched = null
  }
}

export class WorkerMetric extends Metric {
  constructor (_options) {
    super()
    // TODO: revisit this Node-specific fix so TS definitions preserve Worker type info
    const runtimeHasWorkers = globalThis?.isOroRuntime === true

    const BaseWorker =
      runtimeHasWorkers && typeof globalThis.Worker === 'function'
        ? globalThis.Worker
        : class {}

    this.GlobalWorker = BaseWorker
    this.channel = dc.channel('Worker')
    this.Worker = class Worker extends this.GlobalWorker {
      constructor (url, options, ...args) {
        super(url, options, ...args)
        dc.channel('Worker').publish({ worker: this, url, options })
      }
    }
  }

  init () {
    globalThis.Worker = this.Worker
  }

  destroy () {
    this.channel.reset()
    globalThis.Worker = this.GlobalWorker
  }
}

// eslint-disable-next-line new-parens
export const metrics = new (class Metrics {
  // metrics
  requestAnimationFrame = new RequestAnimationFrameMetric()
  XMLHttpRequest = new XMLHttpRequestMetric()
  Worker = new WorkerMetric()
  fetch = new FetchMetric()

  channel = dc

  subscribe (...args) {
    return dc.subscribe(...args)
  }

  unsubscribe (...args) {
    return dc.unsubscribe(...args)
  }

  start (which) {
    if (Array.isArray(which)) {
      for (const key of which) {
        if (typeof this[key]?.init === 'function') {
          this[key].init()
        }
      }
    } else {
      for (const value of Object.values(this)) {
        if (typeof value?.init === 'function') {
          value.init()
        }
      }
    }
  }

  stop (which) {
    if (Array.isArray(which)) {
      for (const key of which) {
        if (typeof this[key]?.destroy === 'function') {
          this[key].destroy()
        }
      }
    } else {
      for (const value of Object.values(this)) {
        if (typeof value?.destroy === 'function') {
          value.destroy()
        }
      }
    }
  }
})()

// make construction illegal
Object.assign(Object.getPrototypeOf(metrics), {
  constructor: IllegalConstructor
})

export default { metrics }
