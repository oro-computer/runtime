import { test } from 'oro:test'
import fs from 'oro:fs'
import os from 'oro:os'
import path from 'oro:path'

const TMPDIR = `${os.tmpdir()}${path.sep}`

test('fs.statSync returns stats with size', async (t) => {
  const file = path.join(TMPDIR, `fs-stats-${Date.now()}.txt`)
  fs.writeFileSync(file, 'stats')
  const stats = fs.statSync(file)
  t.ok(stats.isFile(), 'statSync identifies regular files')
  t.ok(stats.size >= 5, 'statSync exposes file size')
})
