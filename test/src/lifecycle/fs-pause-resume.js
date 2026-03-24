import { test } from 'oro:test'
import fs from 'oro:fs'
import os from 'oro:os'
import path from 'oro:path'

function tmpFilePath (name) {
  const dir = os.tmpdir()
  return path.join(dir, `oro-test-${name}-${Date.now()}.txt`)
}

function sleep (ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

async function write (file, data) {
  await fs.promises.writeFile(file, data, { encoding: 'utf8' })
}

test('fs.watch: survives pause by recreating watcher after resume', async (t) => {
  const file = tmpFilePath('fs-pause-resume')
  await write(file, 'initial')

  const watcher = new fs.Watcher(file)
  const firstChange = new Promise((resolve) => watcher.once('change', resolve))
  await write(file, 'first-change')
  await Promise.race([
    firstChange,
    new Promise((_resolve, reject) =>
      setTimeout(
        () => reject(new Error('timeout waiting for first change')),
        1500
      )
    )
  ])

  // Simulate app pause/resume around a change
  globalThis.dispatchEvent(new Event('applicationpause'))
  await sleep(50)
  const noEvent = await Promise.race([
    new Promise((resolve) => setTimeout(() => resolve(true), 200)),
    new Promise((_resolve, reject) =>
      watcher.once('change', () =>
        reject(new Error('unexpected change during pause'))
      )
    )
  ])
  t.equal(noEvent, true, 'no change observed during pause')

  globalThis.dispatchEvent(new Event('applicationresume'))
  await sleep(150)

  // Existing watcher is stopped by services; recreate and verify it works
  const watcher2 = new fs.Watcher(file)
  const secondChange = new Promise((resolve) =>
    watcher2.once('change', resolve)
  )
  await write(file, 'second-change')
  await Promise.race([
    secondChange,
    new Promise((_resolve, reject) =>
      setTimeout(
        () =>
          reject(new Error('timeout waiting for second change after resume')),
        1500
      )
    )
  ])

  await watcher2.close()
  await watcher.close()
  await fs.promises.unlink(file)
})
