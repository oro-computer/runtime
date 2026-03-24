import { test } from 'oro:test'

function sleep (ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}
test('lifecycle: event order pause→resume→stop', async (t) => {
  const seen = []
  const onPause = () => seen.push('pause')
  const onResume = () => seen.push('resume')
  const onStop = () => seen.push('stop')

  globalThis.addEventListener('applicationpause', onPause)
  globalThis.addEventListener('applicationresume', onResume)
  globalThis.addEventListener('applicationstop', onStop)

  // Dispatch in order with slight spacing to keep assertions predictable
  globalThis.dispatchEvent(new Event('applicationpause'))
  await sleep(10)
  globalThis.dispatchEvent(new Event('applicationresume'))
  await sleep(10)
  globalThis.dispatchEvent(new Event('applicationstop'))
  await sleep(10)

  t.equal(Array.isArray(seen), true, 'collected event list')
  t.equal(seen.length, 3, 'saw three lifecycle events')
  t.equal(seen[0], 'pause', 'first pause')
  t.equal(seen[1], 'resume', 'then resume')
  t.equal(seen[2], 'stop', 'then stop')

  globalThis.removeEventListener('applicationpause', onPause)
  globalThis.removeEventListener('applicationresume', onResume)
  globalThis.removeEventListener('applicationstop', onStop)
})

test('lifecycle: multiple listeners receive events', async (t) => {
  let pauseCountA = 0
  let pauseCountB = 0
  let resumeCountA = 0
  let resumeCountB = 0

  const onPauseA = () => {
    pauseCountA++
  }
  const onPauseB = () => {
    pauseCountB++
  }
  const onResumeA = () => {
    resumeCountA++
  }
  const onResumeB = () => {
    resumeCountB++
  }

  globalThis.addEventListener('applicationpause', onPauseA)
  globalThis.addEventListener('applicationpause', onPauseB)
  globalThis.addEventListener('applicationresume', onResumeA)
  globalThis.addEventListener('applicationresume', onResumeB)

  globalThis.dispatchEvent(new Event('applicationpause'))
  await sleep(5)
  globalThis.dispatchEvent(new Event('applicationresume'))
  await sleep(5)

  t.equal(pauseCountA, 1, 'pause listener A fired once')
  t.equal(pauseCountB, 1, 'pause listener B fired once')
  t.equal(resumeCountA, 1, 'resume listener A fired once')
  t.equal(resumeCountB, 1, 'resume listener B fired once')

  globalThis.removeEventListener('applicationpause', onPauseA)
  globalThis.removeEventListener('applicationpause', onPauseB)
  globalThis.removeEventListener('applicationresume', onResumeA)
  globalThis.removeEventListener('applicationresume', onResumeB)
})
