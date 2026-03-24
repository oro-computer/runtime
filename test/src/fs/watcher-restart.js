import { test } from 'oro:test'
import fs from 'oro:fs'
import os from 'oro:os'
import path from 'oro:path'

function tmpFilePath (name) {
  const dir = os.tmpdir()
  return path.join(dir, `oro-test-${name}-${Date.now()}.txt`)
}

async function write (file, data) {
  await fs.promises.writeFile(file, data, { encoding: 'utf8' })
}

test('fs.watch can stop and restart cleanly', async (t) => {
  const file = tmpFilePath('watch-restart')
  await write(file, 'initial')

  // first watcher
  const first = new fs.Watcher(file)
  const firstEvent = new Promise((resolve) => {
    first.on('change', (evt, filename) => resolve({ evt, filename }))
  })

  await write(file, 'first-change')
  const ev1 = await Promise.race([
    firstEvent,
    new Promise((resolve, reject) =>
      setTimeout(
        () => reject(new Error('timeout waiting for first change')),
        1000
      )
    )
  ])

  t.equal(ev1.evt, 'change', 'first change delivered')

  await first.close()

  // second watcher
  const second = new fs.Watcher(file)
  const secondEvent = new Promise((resolve) => {
    second.on('change', (evt, filename) => resolve({ evt, filename }))
  })

  await write(file, 'second-change')
  const ev2 = await Promise.race([
    secondEvent,
    new Promise((resolve, reject) =>
      setTimeout(
        () => reject(new Error('timeout waiting for second change')),
        1000
      )
    )
  ])

  t.equal(ev2.evt, 'change', 'second change delivered after restart')

  await second.close()
  await fs.promises.unlink(file)
})
