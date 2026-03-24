import { test } from 'oro:test'
import fs from 'oro:fs'
import os from 'oro:os'
import path from 'oro:path'

const TMPDIR = `${os.tmpdir()}${path.sep}`
const FIXTURES = /android/i.test(os.platform())
  ? '/data/local/tmp/oro-test-fixtures/'
  : `${TMPDIR}oro-test-fixtures${path.sep}`

// Async promises API with bigint option
test('fs.promises.fstat bigint returns bigints', async (t) => {
  const fh = await fs.promises.open(FIXTURES + 'file.txt', 'r')
  const st = await fs.promises.fstat(fh, { bigint: true })
  t.equal(typeof st.size, 'bigint', 'size is bigint')
  t.equal(typeof st.mtimeMs, 'bigint', 'mtimeMs is bigint')
  await fh.close()
})

// Sync API with bigint option
test('fs.fstatSync bigint returns bigints', async (t) => {
  const fd = await new Promise((resolve, reject) => {
    fs.open(FIXTURES + 'file.txt', 'r', (err, fd) =>
      err ? reject(err) : resolve(fd)
    )
  })
  const st = fs.fstatSync(fd, { bigint: true })
  t.equal(typeof st.size, 'bigint', 'size is bigint')
  t.equal(typeof st.atimeMs, 'bigint', 'atimeMs is bigint')
  await new Promise((resolve) => fs.close(fd, () => resolve()))
})

// Compare bigint vs number modes for the same file
test('fs.promises.fstat bigint vs number parity', async (t) => {
  const fh = await fs.promises.open(FIXTURES + 'file.txt', 'r')
  const stBig = await fs.promises.fstat(fh, { bigint: true })
  const stNum = await fs.promises.fstat(fh)
  t.ok(
    Number(stBig.size) === stNum.size,
    'size parity between bigint and number'
  )
  await fh.close()
})
