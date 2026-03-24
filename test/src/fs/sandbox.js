import { test } from 'oro:test'
import os from 'oro:os'
import fs from 'oro:fs'
import path from 'oro:path'
import process from 'oro:process'

// Opt-in: enable FS sandbox via env for this test file
process.env.FS_SANDBOX = '1'

const platform = os.platform()

test('fs sandbox: denies outside allowed roots', async (t) => {
  const target =
    platform === 'win32'
      ? 'C\\\x3A\\Windows\\System32\\drivers\\etc\\hosts'
      : '/etc/hosts'

  await new Promise((resolve) => {
    fs.readFile(target, 'utf8', (err) => {
      t.ok(err, 'readFile outside allowed roots is denied with error')
      resolve()
    })
  })

  await new Promise((resolve) => {
    fs.access(target, fs.constants.R_OK, (err) => {
      t.ok(err, 'access outside allowed roots is denied with error')
      resolve()
    })
  })
})

test('fs sandbox: allows tmpdir operations', async (t) => {
  const TMPDIR = `${os.tmpdir()}${path.sep}`
  const file = path.join(TMPDIR, `sandbox-allow-${Date.now()}.txt`)

  await new Promise((resolve) => {
    fs.writeFile(file, 'hello', (err) => {
      t.ok(!err, 'writeFile in tmpdir allowed')
      resolve()
    })
  })

  await new Promise((resolve) => {
    fs.readFile(file, 'utf8', (err, data) => {
      t.ok(!err && data === 'hello', 'readFile in tmpdir allowed and correct')
      resolve()
    })
  })
})
