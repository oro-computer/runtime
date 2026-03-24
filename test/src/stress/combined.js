import { test } from 'oro:test'
import os from 'oro:os'
import path from 'oro:path'
import fs from 'oro:fs'
import dgram from 'oro:dgram'
import crypto from 'oro:crypto'

function sleep (ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

function tmpFilePath (name) {
  const dir = os.tmpdir()
  return path.join(
    dir,
    `oro-stress-${name}-${Date.now()}-${Math.random().toString(16).slice(2)}.txt`
  )
}

test('stress: timers + dgram + fs watcher', async (t) => {
  // Timers burst
  let immediateCount = 0
  let timeoutsFired = 0
  const intervals = []
  const timeouts = []

  for (let i = 0; i < 100; i++) {
    setImmediate(() => {
      immediateCount++
    })
  }
  for (let i = 0; i < 50; i++) {
    timeouts.push(
      setTimeout(() => {
        timeoutsFired++
      }, 5)
    )
  }
  for (let i = 0; i < 5; i++) intervals.push(setInterval(() => {}, 10))

  await sleep(60)
  intervals.forEach(clearInterval)
  timeouts.forEach(clearTimeout) // after having fired

  t.ok(immediateCount === 100, 'all immediates fired')
  t.ok(timeoutsFired === 50, 'all timeouts fired once')

  // UDP burst (skip on certain CI platforms)
  if (
    !(os.platform() === 'win32' && process.env.GITHUB_ACTIONS_CI) &&
    !process.env.ORO_ANDROID_CI
  ) {
    const server = dgram.createSocket({ type: 'udp4', reuseAddr: true })
    const client = dgram.createSocket({ type: 'udp4', reuseAddr: true })
    const address = '127.0.0.1'
    const port = 32000 + Math.floor(Math.random() * 1000)
    const total = 128
    let received = 0

    const ready = new Promise((resolve) => server.once('listening', resolve))
    const all = new Promise((resolve, reject) => {
      const to = setTimeout(
        () => reject(new Error('udp receive timeout')),
        2000
      )
      server.on('message', () => {
        received++
        if (received === total) {
          clearTimeout(to)
          resolve()
        }
      })
      server.on('error', reject)
    })

    server.bind(port, address)
    await ready

    for (let i = 0; i < total; i++) {
      const buf = crypto.randomBytes(32)
      await new Promise((resolve) =>
        client.send(buf, port, address, () => resolve())
      )
    }

    await all
    t.equal(received, total, 'received all UDP packets')

    await new Promise((resolve) => server.close(resolve))
    await new Promise((resolve) => client.close(resolve))
  } else {
    t.comment('udp burst skipped on this platform')
  }

  // FS watcher start -> changes -> restart -> more changes
  const file = tmpFilePath('fs-watch')
  await fs.promises.writeFile(file, 'initial', 'utf8')

  const first = new fs.Watcher(file)
  const firstEv = new Promise((resolve, reject) => {
    const to = setTimeout(
      () => reject(new Error('timeout waiting for fs change (first)')),
      1500
    )
    first.on('change', (evt) => {
      clearTimeout(to)
      resolve(evt)
    })
  })
  await fs.promises.writeFile(file, 'change-1', 'utf8')
  await firstEv
  await first.close()

  const second = new fs.Watcher(file)
  const secondEv = new Promise((resolve, reject) => {
    const to = setTimeout(
      () => reject(new Error('timeout waiting for fs change (second)')),
      1500
    )
    second.on('change', (evt) => {
      clearTimeout(to)
      resolve(evt)
    })
  })
  await fs.promises.writeFile(file, 'change-2', 'utf8')
  await secondEv
  await second.close()
  await fs.promises.unlink(file)

  t.ok(true, 'fs watcher handled restart and events')
})
