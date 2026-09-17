import { test } from 'oro:test'
import dgram from 'oro:dgram'

function sleep (ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

function exchange (server, client, message, ...destination) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      finish(new Error(`UDP pause/resume timed out receiving "${message}"`))
    }, 10000)
    const onmessage = buffer => finish(null, buffer.toString())
    const onerror = error => finish(error)
    function finish (error, value) {
      clearTimeout(timer)
      server.removeListener('message', onmessage)
      server.removeListener('error', onerror)
      client.removeListener('error', onerror)
      if (error) reject(error)
      else resolve(value)
    }
    server.once('message', onmessage)
    server.once('error', onerror)
    client.once('error', onerror)
    try {
      client.send(message, ...destination, error => {
        if (error) finish(error)
      })
    } catch (error) {
      finish(error)
    }
  })
}

async function checkPauseResume (t, connected) {
  const address = '127.0.0.1'
  const server = dgram.createSocket('udp4')
  const client = dgram.createSocket('udp4')

  try {
    await new Promise((resolve, reject) => {
      server.bind(0, address, error => error ? reject(error) : resolve())
    })
    const port = server.address().port
    if (connected) {
      await new Promise((resolve, reject) => {
        client.connect(port, address, error => error ? reject(error) : resolve())
      })
    }
    const destination = connected ? [] : [port, address]
    t.equal(await exchange(server, client, 'before', ...destination), 'before', 'received before pause')

    // Conduit may still be reconnecting when the next datagram arrives.
    globalThis.dispatchEvent(new Event('applicationpause'))
    await sleep(200)
    globalThis.dispatchEvent(new Event('applicationresume'))
    await sleep(500)
    t.equal(await exchange(server, client, 'after', ...destination), 'after', 'received after resume')
  } finally {
    await Promise.all([server, client].map(socket => new Promise(resolve => socket.close(resolve))))
  }
}

test('udp: bound socket receives before and after pause/resume', async (t) => {
  await checkPauseResume(t, false)
})

test('udp: connected client sends after pause/resume', async (t) => {
  await checkPauseResume(t, true)
})
