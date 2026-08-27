import { pathToFileURL } from 'node:url'

const runtimeNodeSpecifier = process.env.ORO_RUNTIME_NODE_PATH
  ? pathToFileURL(process.env.ORO_RUNTIME_NODE_PATH).href
  : '@oro-computer/runtime-node'
const { default: socket } = await import(runtimeNodeSpecifier)

socket.on('20 minutes adventure', async (value) => {
  await socket.send({
    window: 0,
    event: '20 minutes adventure',
    value
  })
})

await socket.send({
  window: 0,
  event: 'backend:ready'
})
await socket.send({
  window: 0,
  event: 'character',
  value: { firstname: 'Morty', secondname: 'Smith' }
})
