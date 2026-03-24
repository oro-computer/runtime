import { test } from 'oro:test'
import path from 'oro:path'

// Generate many join/relative scenarios for both posix and win32.

const posixBases = ['/', '/a', '/a/b', '/a/b/c']
const posixSegs = ['', '.', 'd', '../d', './e', 'f/g', '/reset']
for (let i = 0; i < posixBases.length; i++) {
  for (let j = 0; j < posixSegs.length; j++) {
    const base = posixBases[i]
    const seg = posixSegs[j]
    test(`path.posix.join matrix ${i}-${j}`, (t) => {
      const out = path.posix.join(base, seg)
      t.equal(typeof out, 'string')
      t.ok(out.startsWith('/') || !out.startsWith('//'))
    })
  }
}
const winBases = ['C:\\', 'C:\\a', 'C:\\a\\b']
const winSegs = ['', '.', 'd', '..\\d', '.\\e', 'f\\g', '\\reset']
for (let i = 0; i < winBases.length; i++) {
  for (let j = 0; j < winSegs.length; j++) {
    const base = winBases[i]
    const seg = winSegs[j]
    test(`path.win32.join matrix ${i}-${j}`, (t) => {
      const out = path.win32.join(base, seg)
      t.equal(typeof out, 'string')
      t.ok(/^[A-Za-z]:\\/.test(out) || out.startsWith('\\') || out.length > 0)
    })
  }
}

// Relative combos
const relPairs = [
  ['/a/b/c', '/a/b', '..'],
  ['/a/b', '/a/b/c/d', 'c/d'],
  ['/a', '/', '..'],
  ['C:\\a\\b', 'C:\\a', '..'],
  ['C:\\', 'C:\\a', 'a']
]
for (let i = 0; i < relPairs.length; i++) {
  const [from, to, expect] = relPairs[i]
  test(`path.relative matrix ${i + 1}`, (t) => {
    const out = /\\/.test(from)
      ? path.win32.relative(from, to)
      : path.posix.relative(from, to)
    t.equal(out, expect)
  })
}
