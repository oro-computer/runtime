import diagnostics from 'oro:diagnostics'
import { RequestAnimationFrameMetric } from 'oro:diagnostics/window'
import test from 'oro:test'

test('diagnostics - distinct frame timestamps and rolling samples', (t) => {
  const original = globalThis.requestAnimationFrame
  const callbacks = []
  globalThis.requestAnimationFrame = callback => callbacks.push(callback)
  const metric = new RequestAnimationFrameMetric({ sampleSize: 3 })
  try {
    metric.init()
    let calls = 0
    globalThis.requestAnimationFrame(() => calls++)
    globalThis.requestAnimationFrame(() => calls++)
    // eslint-disable-next-line n/no-callback-literal
    callbacks.splice(0).forEach(callback => callback(0))
    t.equal(calls, 2, 'all callbacks execute')
    t.equal(metric.value, null, 'first distinct frame establishes the baseline')
    metric.update(10)
    metric.update(10)
    metric.update(20)
    metric.update(30)
    t.equal(metric.sampleTick, 0, 'cursor wraps after a full buffer')
    t.equal(metric.value.samples, 3, 'sample count survives wrapping')
    t.equal(metric.value.rate, 100, 'full buffer has a finite rate')
    metric.update(50)
    t.equal(metric.value.rate, 75, 'oldest interval is replaced in the average')
    metric.update(NaN)
    metric.update(Infinity)
    metric.update(40)
    t.equal(metric.value.samples, 3, 'invalid and out-of-order timestamps are ignored')
    globalThis.requestAnimationFrame(() => calls++)
    const staleCallback = callbacks.shift()
    metric.destroy()
    metric.init()
    metric.update(0)
    metric.update(1)
    t.equal(metric.value.rate, 1000, 'high rates do not overflow an eight-bit sample')
    t.equal(metric.value.samples, 1, 'restart clears earlier samples')
    // eslint-disable-next-line n/no-callback-literal
    staleCallback(500)
    t.equal(metric.now, 1, 'callbacks scheduled before restart cannot add new samples')
    metric.destroy()
    metric.update(2)
    t.equal(metric.value, null, 'late callbacks after stop do not publish')
    t.equal(metric.toJSON().samples.length, 0, 'stopped metrics serialize safely')
  } finally {
    metric.destroy()
    globalThis.requestAnimationFrame = original
  }
})

test('diagnostics - interleaved frame metric lifetimes preserve scheduling', (t) => {
  const original = globalThis.requestAnimationFrame
  const callbacks = []
  globalThis.requestAnimationFrame = callback => callbacks.push(callback)
  const first = new RequestAnimationFrameMetric()
  const second = new RequestAnimationFrameMetric()
  try {
    first.init()
    second.init()
    first.destroy()
    let calls = 0
    globalThis.requestAnimationFrame(() => calls++)
    // eslint-disable-next-line n/no-callback-literal
    callbacks.shift()(10)
    t.equal(calls, 1, 'outer instrumentation still forwards after the inner metric stops')
    first.init()
    second.destroy()
    globalThis.requestAnimationFrame(() => calls++)
    // eslint-disable-next-line n/no-callback-literal
    callbacks.shift()(20)
    t.equal(calls, 2, 'restart does not create a scheduler cycle')
    t.equal(first.now, 20, 'the restarted metric observes the current frame')
    t.equal(second.value, null, 'stopped instrumentation does not resume sampling')
  } finally {
    first.destroy()
    second.destroy()
    globalThis.requestAnimationFrame = original
  }
})

test('diagnostics - window - metrics', async (t) => {
  diagnostics.window.metrics.start()

  await new Promise((resolve) => {
    const options = { method: 'GET' }
    const uri = '/index.html'
    let diagnosticsReceived = false

    diagnostics.window.metrics.subscribe('fetch', (message) => {
      t.equal(message.resource, uri, 'message.resource === uri')
      t.equal(message.options, options, 'message.options === options')
      diagnosticsReceived = true
    })

    fetch(uri, options)
      .then((response) => response.text())
      .then((text) => t.ok(typeof text === 'string', 'response.text()'))
      .then(() => t.ok(diagnosticsReceived, 'fetch diagnostics received'))
      .catch((err) => t.ifError(err))
      .then(resolve)
  })

  await new Promise((resolve) => {
    const pending = []
    const method = 'GET'
    const uri = window.location.href

    pending.push(
      new Promise((resolve) => {
        diagnostics.window.metrics.subscribe(
          'XMLHttpRequest.open',
          (message) => {
            t.ok(message, 'window.XMLHttpRequest.open')
            resolve()
          }
        )
      })
    )

    pending.push(
      new Promise((resolve) => {
        diagnostics.window.metrics.subscribe(
          'XMLHttpRequest.send',
          (message) => {
            t.ok(message, 'window.XMLHttpRequest.send')
            resolve()
          }
        )
      })
    )

    const request = new globalThis.XMLHttpRequest()
    request.open(method, uri, true)
    request.send()

    Promise.all(pending).then(resolve)
  })

  diagnostics.window.metrics.stop()
})
