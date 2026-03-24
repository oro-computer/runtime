import { test } from 'oro:test'
import fs from 'oro:fs'
import os from 'oro:os'
import path from 'oro:path'

const TMPDIR = `${os.tmpdir()}${path.sep}`

test('fs.opendir yields Dir entries', async (t) => {
  const dir = path.join(TMPDIR, `fs-dir-${Date.now()}`)
  fs.mkdirSync(dir, { recursive: true })
  fs.writeFileSync(path.join(dir, 'file.txt'), 'dir')

  await new Promise((resolve) => {
    fs.opendir(dir, (err, d) => {
      if (err) {
        t.fail(err)
        return resolve()
      }

      d.read((err2, entry) => {
        if (err2) t.fail(err2)
        else t.equal(entry.name, 'file.txt', 'Dir.read returns entries')
        d.close(() => resolve())
      })
    })
  })
})
