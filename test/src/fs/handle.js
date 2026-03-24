import { test } from 'oro:test'
import { FileHandle } from 'oro:fs/handle'

test('fs.FileHandle opening semantics', async (t) => {
  const id = String(Math.random())
  const fh = new FileHandle({ id, flags: 'r', mode: 0o666, handle: {} })
  t.equal(fh.opening, false, 'opening is false before open')
  await fh.open()
  t.equal(fh.opening, false, 'opening is false after open resolves')
  t.equal(fh.opened, true, 'opened is true after open')
  await fh.close()
})
