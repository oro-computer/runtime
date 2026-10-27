import { test } from 'oro:test'
import application from 'oro:application'
import fs from 'oro:fs'
import os from 'oro:os'
import path from 'oro:path'

function sleep (ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}
function tmpFilePath (name) {
  const dir = os.tmpdir()
  return path.join(dir, `oro-test-lifecycle-${name}-${Date.now()}.txt`)
}

async function write (file, data) {
  await fs.promises.writeFile(file, data, { encoding: 'utf8' })
}

// Desktop-only: when lifecycle_desktop_always_running=false, minimize/restore should
// enforce a real pause/resume at native level (FS watchers stopped while minimized).
// This test is enabled only when ORO_LIFECYCLE_TEST_STRICT=1 to avoid modifying
// the primary CI build. The desktop test runner will execute a second pass with
// this env var and modified project configuration.
if (process.env.ORO_LIFECYCLE_TEST_STRICT) {
  test.desktop(
    'lifecycle (desktop config): real pause/resume when always_running=false',
    async (t) => {
      const win = await application.getCurrentWindow()
      const file = tmpFilePath('config-pause')
      await write(file, 'initial')

      const watcher = new fs.Watcher(file, { start: false })
      await watcher.start()
      const firstChange = new Promise((resolve, reject) => {
        const to = setTimeout(
          () => reject(new Error('timeout waiting for first change')),
          1500
        )
        watcher.once('change', () => {
          clearTimeout(to)
          resolve(true)
        })
      })

      await write(file, 'first-change')
      t.equal(await firstChange, true, 'baseline watcher is active')

      // Minimize (pause) and attempt a change; watcher should not fire
      await win.minimize()
      await sleep(100)

      let pausedOk = false
      const pausedChange = Promise.race([
        new Promise((resolve) =>
          setTimeout(() => {
            pausedOk = true
            resolve(true)
          }, 600)
        ),
        new Promise((_resolve, reject) =>
          watcher.once('change', () =>
            reject(new Error('unexpected change during pause'))
          )
        )
      ])
      // Native writes remain pending until the loop resumes.
      const pausedWrite = write(file, 'change-while-paused')
      await pausedChange
      t.equal(pausedOk, true, 'no change observed while paused')

      // Restore (resume) and re-create watcher; then verify changes fire
      await win.restore()
      await pausedWrite
      await sleep(200)

      const watcher2 = new fs.Watcher(file, { start: false })
      await watcher2.start()
      const secondChange = new Promise((resolve, reject) => {
        const to = setTimeout(
          () => reject(new Error('timeout waiting for second change')),
          1500
        )
        watcher2.once('change', () => {
          clearTimeout(to)
          resolve(true)
        })
      })
      await write(file, 'second-change')
      t.equal(await secondChange, true, 'watcher restarted after resume')

      await watcher2.close()
      try {
        await watcher.close()
        t.fail('pause must release the original native watcher')
      } catch (error) {
        t.equal(error.code, 'NOT_FOUND_ERR', 'pause released the original native watcher')
      }
      await fs.promises.unlink(file)
    }
  )
}
