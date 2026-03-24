import { test } from 'oro:test'
import { Worker } from 'oro:worker_threads'

test('worker_threads: process.exit triggers full cleanup', async (t) => {
  const code = `
    postMessage('ok')
    process.exit(0)
  `

  const worker = new Worker(code, { eval: true, stdout: true, stderr: true })

  const events = []
  worker.on('online', () => events.push('online'))
  worker.on('message', (m) => events.push(['message', m]))

  const exitCode = await new Promise((resolve) => {
    worker.on('exit', (code) => resolve(code))
  })

  t.equal(exitCode, 0, 'exit code is 0')
  t.ok(
    events.find((e) => e === 'online'),
    'online event fired'
  )
  t.ok(
    events.find((e) => Array.isArray(e) && e[0] === 'message' && e[1] === 'ok'),
    'message delivered'
  )
  t.equal(worker.stdout, null, 'stdout cleaned up')
  t.equal(worker.stderr, null, 'stderr cleaned up')
})
