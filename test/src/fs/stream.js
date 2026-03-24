import { test } from 'oro:test'
import fs from 'oro:fs'
import { Buffer } from 'oro:buffer'
import os from 'oro:os'
import path from 'oro:path'

test('fs streams are readable and writable', async (t) => {
  if (os.platform() === 'android') {
    t.comment('TODO: enable fs stream test on android')
    return
  }

  const file = path.join(os.tmpdir(), `fs-stream-${Date.now()}.txt`)
  const payload = Buffer.from('stream test')

  await new Promise((resolve) => {
    const ws = fs.createWriteStream(file)
    ws.on('error', (err) => {
      t.fail(err)
      resolve()
    })
    ws.end(payload, () => resolve())
  })

  await new Promise((resolve) => {
    const rs = fs.createReadStream(file)
    const chunks = []
    rs.on('data', (chunk) => chunks.push(chunk))
    rs.on('error', (err) => {
      t.fail(err)
      resolve()
    })
    rs.on('end', () => {
      const buf = Buffer.concat(chunks)
      t.equal(
        buf.toString(),
        payload.toString(),
        'round-tripped data via streams'
      )
      resolve()
    })
  })
})
