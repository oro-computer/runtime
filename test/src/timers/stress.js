import { test } from 'oro:test'

function sleep (ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

test('timers: many timeouts fire once', async (t) => {
  let count = 0
  const ids = []
  for (let i = 0; i < 50; i++) {
    ids.push(
      setTimeout(() => {
        count++
      }, 5)
    )
  }
  await sleep(60)
  t.equal(count, 50, 'all timeouts fired exactly once')
})
test('timers: interval counts and clears', async (t) => {
  let count = 0
  let id
  await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      clearInterval(id)
      reject(new Error('interval did not fire three times within one second'))
    }, 1000)
    id = setInterval(() => {
      count++
      if (count === 3) {
        clearTimeout(timeout)
        resolve()
      }
    }, 5)
  })
  clearInterval(id)
  const final = count
  await sleep(30)
  t.ok(final >= 3, 'interval fired multiple times')
  t.equal(count, final, 'cleared interval did not fire again')
})

test('timers: double clearTimeout safe', async (t) => {
  let fired = false
  const id = setTimeout(() => {
    fired = true
  }, 10)
  clearTimeout(id)
  clearTimeout(id)
  await sleep(30)
  t.equal(fired, false, 'timeout did not fire after double clear')
})

test('timers: multiple immediates fire once', async (t) => {
  let count = 0
  for (let i = 0; i < 20; i++) {
    setImmediate(() => {
      count++
    })
  }
  await sleep(10)
  t.equal(count, 20, 'all immediates fired exactly once')
})
