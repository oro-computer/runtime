import { test } from 'oro:test'
import fs from 'oro:fs'
import os from 'oro:os'
import path from 'oro:path'
import ipc from 'oro:ipc'

function tmpFilePath (name) {
  const dir = os.tmpdir()
  return path.join(dir, `oro-test-${name}-${Date.now()}.txt`)
}

async function write (file, data) {
  await fs.promises.writeFile(file, data, { encoding: 'utf8' })
}

test(os.platform() === 'android'
  ? 'fs.watch reports unsupported on Android'
  : 'fs.watch can stop and restart cleanly', async (t) => {
  const file = tmpFilePath('watch-restart')
  await write(file, 'initial')

  // Android exposes fs.watch through IPC but does not implement native watchers.
  if (os.platform() === 'android') {
    const watcher = new fs.Watcher(file, { start: false })
    const errors = []
    watcher.on('error', error => errors.push(error))
    await watcher.start()
    t.equal(errors.length, 1, 'unsupported watcher emits one error')
    t.equal(errors[0]?.message, 'Not supported', 'unsupported watcher reports its platform limit')
    await t.rejects(watcher.close(), /Not supported/, 'closing a watcher also reports the Android platform limit')
    await fs.promises.unlink(file)
    return
  }

  // first watcher
  const first = new fs.Watcher(file, { start: false })
  const firstEvent = new Promise((resolve) => {
    first.on('change', (evt, filename) => resolve({ evt, filename }))
  })

  await first.start()
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
  const stopped = await ipc.request('fs.stopWatch', { id: first.id })
  t.equal(stopped.err?.name, 'NotFoundError', 'closed watcher is removed from the native registry')

  // second watcher
  const second = new fs.Watcher(file, { start: false })
  const secondEvent = new Promise((resolve) => {
    second.on('change', (evt, filename) => resolve({ evt, filename }))
  })

  await second.start()
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
