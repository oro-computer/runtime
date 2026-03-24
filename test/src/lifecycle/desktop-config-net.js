import { test } from 'oro:test'

function sleep (ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
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
      await win.minimize()
      await sleep(200)

      // send during pause; expect no delivery within short window
      const noRecv = new Promise((resolve, reject) => {
        const to = setTimeout(() => {
          resolve(true)
        }, 600)
        server.once('message', () => {
          clearTimeout(to)
          reject(new Error('unexpected udp during pause'))
        })
      })
      await new Promise((resolve) =>
        client.send('ok-2', port, address, resolve)
      )
      t.equal(await noRecv, true, 'no udp delivery while paused')

      // restore to resume
      await win.restore()
      await sleep(200)

      const got2 = new Promise((resolve, reject) => {
        const to = setTimeout(
          () => reject(new Error('timeout: udp receive after resume')),
          1500
        )
        server.once('message', () => {
          clearTimeout(to)
          resolve(true)
        })
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
      await sleep(100)
      t.equal(messages.includes('hello-1'), true, 'tcp baseline receive ok')

      const win = await application.getCurrentWindow()
      await win.minimize()
      await sleep(200)

      client.write('hello-2')
      await sleep(600)
      t.equal(
        messages.includes('hello-2'),
        false,
        'no tcp data observed while paused'
      )

      await win.restore()
      await sleep(300)

      client.write('hello-3')
      // Give time for delivery
      await sleep(300)
      t.equal(
        messages.includes('hello-3'),
        true,
        'tcp delivery after resume ok'
      )

      client.destroy()
      if (serverSocket) serverSocket.destroy()
      server.close()
    }
  )
}
