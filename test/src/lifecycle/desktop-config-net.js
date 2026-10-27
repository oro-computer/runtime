import { test } from 'oro:test'

function sleep (ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

async function waitFor (predicate) {
  const deadline = Date.now() + 1500
  while (!predicate() && Date.now() < deadline) await sleep(10)
  return predicate()
}

// Only run in strict mode (second pass) and desktop
if (process.env.ORO_LIFECYCLE_TEST_STRICT) {
  test.desktop(
    'strict lifecycle: UDP paused when minimized; resumes after restore',
    async (t) => {
      const dgram = await import('oro:dgram')
      const application = (await import('oro:application')).default
      const address = '127.0.0.1'
      const port = 45321

      const server = dgram.createSocket({ type: 'udp4', reuseAddr: true })
      const client = dgram.createSocket('udp4')

      await new Promise((resolve, reject) => {
        server.once('error', reject)
        server.bind(port, address, resolve)
      })

      // baseline: ensure receive works
      const got1 = new Promise((resolve, reject) => {
        const to = setTimeout(
          () => reject(new Error('timeout: baseline udp receive')),
          1500
        )
        server.once('message', () => {
          clearTimeout(to)
          resolve(true)
        })
      })
      await new Promise((resolve) =>
        client.send('ok-1', port, address, resolve)
      )
      t.equal(await got1, true, 'udp baseline receive ok')

      // minimize to trigger native pause
      const win = await application.getCurrentWindow()
      t.comment('UDP: minimizing window')
      await win.minimize()
      await sleep(200)

      // Queue native I/O while paused; awaiting its callback here would prevent restore.
      const pausedMessages = []
      const onPausedMessage = message => pausedMessages.push(String(message))
      server.on('message', onPausedMessage)
      t.comment('UDP: queuing send while paused')
      const pausedSend = new Promise((resolve, reject) =>
        client.send('ok-2', port, address, error => error ? reject(error) : resolve())
      )
      await sleep(600)
      t.equal(pausedMessages.length, 0, 'no udp delivery while paused')
      server.removeListener('message', onPausedMessage)

      // restore to resume
      await Promise.race([
        win.restore(),
        sleep(3000).then(() => { throw new Error('timeout: restore IPC') })
      ])
      t.pass('restore IPC settles')
      await Promise.race([
        pausedSend,
        sleep(3000).then(() => { throw new Error('timeout: paused UDP send after restore') })
      ])
      await sleep(200)

      const got2 = new Promise((resolve, reject) => {
        const to = setTimeout(
          () => reject(new Error('timeout: udp receive after resume')),
          1500
        )
        const onMessage = message => {
          if (String(message) !== 'ok-3') return
          server.removeListener('message', onMessage)
          clearTimeout(to)
          resolve(true)
        }
        server.on('message', onMessage)
      })
      await new Promise((resolve) =>
        client.send('ok-3', port, address, resolve)
      )
      t.equal(await got2, true, 'udp delivery after resume ok')

      server.close()
      client.close()
    }
  )

  test.desktop(
    'strict lifecycle: TCP paused when minimized; resumes after restore',
    async (t) => {
      const net = await import('oro:net')
      const application = (await import('oro:application')).default
      const address = '127.0.0.1'
      const port = 45322

      // server
      const server = net.createServer()
      await new Promise((resolve, reject) => {
        server.once('error', reject)
        server.listen({ port, host: address }, resolve)
      })

      // accept and capture data
      let serverSocket = null
      const messages = []
      server.on('connection', (sock) => {
        serverSocket = sock
        sock.on('data', (buf) => messages.push(String(buf)))
      })

      // client and baseline
      const client = net.createConnection({ port, host: address })
      await new Promise((resolve, reject) => {
        client.once('error', reject)
        client.once('connect', resolve)
      })
      client.write('hello-1')
      t.equal(await waitFor(() => messages.join('').includes('hello-1')), true, 'tcp baseline receive ok')

      const win = await application.getCurrentWindow()
      t.comment('TCP: minimizing window')
      await win.minimize()
      await sleep(200)

      t.comment('TCP: queuing write while paused')
      client.write('hello-2')
      await sleep(600)
      t.equal(
        messages.join('').includes('hello-2'),
        false,
        'no tcp data observed while paused'
      )

      await win.restore()
      await sleep(300)

      client.write('hello-3')
      t.equal(
        await waitFor(() => messages.join('').includes('hello-3')),
        true,
        'tcp delivery after resume ok'
      )

      client.destroy()
      if (serverSocket) serverSocket.destroy()
      server.close()
    }
  )
}
