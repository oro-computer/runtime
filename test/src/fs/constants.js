import { test } from 'oro:test'
import fs from 'oro:fs'

test('fs.constants exposes expected flags', async (t) => {
  t.ok(fs.constants, 'fs.constants is defined')
  t.equal(typeof fs.constants.O_RDONLY, 'number', 'O_RDONLY is a number')
})
