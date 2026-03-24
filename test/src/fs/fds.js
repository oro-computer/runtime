import { test } from 'oro:test'
import fs from 'oro:fs'
import os from 'oro:os'
import path from 'oro:path'

const TMPDIR = `${os.tmpdir()}${path.sep}`

test('fs.open returns a usable fd', async (t) => {
  const file = path.join(TMPDIR, `fs-fd-${Date.now()}.txt`)
  fs.writeFileSync(file, 'fd')

  await new Promise((resolve) => {
    fs.open(file, (err, fd) => {
      if (err) {
        t.fail(err)
        return resolve()
      }

      t.ok(Number.isFinite(fd), 'fs.open returns numeric fd')
      fs.close(fd, (err2) => {
        if (err2) t.fail(err2)
        resolve()
      })
    })
  })
})
