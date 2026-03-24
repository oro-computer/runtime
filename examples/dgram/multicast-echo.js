import dgram, { getCapabilities } from 'oro:dgram'
import Buffer from 'oro:buffer'

const GROUP = '239.255.0.123'
const PORT = 45001

function defaultLog (message) {
  console.log(`[dgram] ${message}`)
}

export async function runMulticastEcho ({
  message = 'hello multicast',
  log = defaultLog
} = {}) {
  const caps = await getCapabilities()
  if (!caps?.multicast) {
    log('multicast not supported on this platform')
    return () => {}
  }

  const socket = dgram.createSocket({ type: 'udp4', reuseAddr: true })

  await new Promise((resolve, reject) => {
    socket.once('error', reject)
    socket.bind(PORT, '0.0.0.0', resolve)
  })

  await socket.setMulticastLoopback(true)
  await socket.setMulticastTTL(1)
  await socket.addMembership(GROUP)
  log(`joined ${GROUP}:${PORT}`)

  const close = () => {
    try {
      socket.close()
    } catch {}
  }

  socket.once('message', (msg) => {
    log(`received -> ${Buffer.from(msg).toString()}`)
    close()
  })

  await new Promise((resolve, reject) => {
    socket.send(Buffer.from(message), PORT, GROUP, (err) => {
      if (err) reject(err)
      else resolve()
    })
  })

  log(`sent -> ${message}`)
  return close
}

export default runMulticastEcho
