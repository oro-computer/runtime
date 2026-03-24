import { test } from 'oro:test'
import fs from 'oro:fs'

test('fs binding exposes expected methods', async (t) => {
  t.equal(typeof fs.open, 'function', 'fs.open is a function')
  t.equal(typeof fs.close, 'function', 'fs.close is a function')
})
