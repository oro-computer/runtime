import { test } from 'oro:test'

test('timers: clearTimeout prevents execution', async (t) => {
  let fired = false
  const id = setTimeout(() => {
    fired = true
  }, 50)
  clearTimeout(id)
  await new Promise((resolve) => setTimeout(resolve, 80))
  t.ok(!fired, 'timeout callback did not fire after clearTimeout')
})
test('timers: setInterval fires and can be cleared', async (t) => {
  let count = 0
  const id = setInterval(() => {
    count++
  }, 20)
  await new Promise((resolve) => setTimeout(resolve, 95))
  clearInterval(id)
  const final = count
  t.ok(final > 0 && final <= 5, 'interval fired a few times then was cleared')
})

test('timers: setImmediate fires exactly once', async (t) => {
  let count = 0
  setImmediate(() => {
    count++
  })
  await new Promise((resolve) => setTimeout(resolve, 10))
  t.equal(count, 1, 'immediate fired once')
})
