import { test } from 'oro:test'
import path from 'oro:path'

// Extra coverage for join/normalize/relative/isAbsolute across edge cases.

// POSIX join edge cases
const posixJoinCases = [
  [['', 'a'], 'a'],
  [['.', 'a'], 'a'],
  [['a', '.'], 'a'],
  [['a/', '/b'], '/b'],
  [['/a/', '/b/'], '/b/'],
  [['/a//', 'b//c'], '/a/b/c'],
  [['//', 'a', 'b'], '/a/b'],
  [['/a', '..', 'b'], '/b'],
  [['/a', '../', 'b'], '/b'],
  [['http://ex.com/a', 'b'], 'http://ex.com/a/b'],
  [['http://ex.com/a/', '../b'], 'http://ex.com/b'],
  [['file:///a/b', 'c', '..', 'd'], 'file:///a/b/d']
]
for (let i = 0; i < posixJoinCases.length; i++) {
  const [args, expect] = posixJoinCases[i]
  test(`path.posix.join extra ${i + 1}`, (t) => {
    t.equal(path.posix.join(...args), expect)
  })
}
// POSIX normalize edge cases
const posixNormalize = [
  ['//a///b//c', '/a/b/c'],
  ['/a/b/./c', '/a/b/c'],
  ['/a/b/../c/', '/a/c/'],
  ['a//b//c', 'a/b/c'],
  ['./a/b', 'a/b'],
  ['../a/b', '../a/b']
]
for (let i = 0; i < posixNormalize.length; i++) {
  const [input, expect] = posixNormalize[i]
  test(`path.posix.normalize extra ${i + 1}`, (t) => {
    t.equal(path.posix.normalize(input), expect)
  })
}

// POSIX relative edge cases
const posixRelative = [
  ['/a/b', '/a/b', ''],
  ['/a/b', '/a/b/c', 'c'],
  ['/a/b/c', '/a/b', '..'],
  ['/a/b/c', '/a/d/e', '../../d/e'],
  ['/', '/a', 'a'],
  ['/a', '/', '..']
]
for (let i = 0; i < posixRelative.length; i++) {
  const [from, to, expect] = posixRelative[i]
  test(`path.posix.relative extra ${i + 1}`, (t) => {
    t.equal(path.posix.relative(from, to), expect)
  })
}

// POSIX dirname/basename/extname edge cases
const posixParts = [
  ['/.hidden', '/', '.hidden', ''],
  ['/a/.b', '/a', '.b', ''],
  ['/a/b.', '/a', 'b.', '.'],
  ['/a/b.c.d', '/a', 'b.c.d', '.d']
]
for (let i = 0; i < posixParts.length; i++) {
  const [full, dir, base, ext] = posixParts[i]
  test(`path.posix.dirname extra ${i + 1}`, (t) => {
    t.equal(path.posix.dirname(full), dir)
  })
  test(`path.posix.basename extra ${i + 1}`, (t) => {
    t.equal(path.posix.basename(full), base)
  })
  test(`path.posix.extname extra ${i + 1}`, (t) => {
    t.equal(path.posix.extname(full), ext)
  })
}

// Windows join/normalize/relative edge cases
const winJoinCases = [
  [['', 'a'], 'a'],
  [['.', 'a'], 'a'],
  [['a', '.'], 'a'],
  [['C:\\a', 'b'], 'C:\\a\\b'],
  [['C:\\a\\', '\\b'], 'C:\\b'],
  [['C:\\a', '..\\b'], 'C:\\b'],
  [['\\\\server\\share', 'a', 'b'], '\\\\server\\share\\a\\b']
]
for (let i = 0; i < winJoinCases.length; i++) {
  const [args, expect] = winJoinCases[i]
  test(`path.win32.join extra ${i + 1}`, (t) => {
    t.equal(path.win32.join(...args), expect)
  })
}

const winNormalize = [
  ['C:\\a\\.\\b', 'C:\\a\\b'],
  ['C:\\a\\..\\b', 'C:\\b'],
  ['C:foo\\..\\bar', 'C:bar'],
  ['\\\\server\\share\\a\\..\\b', '\\\\server\\share\\b']
]
for (let i = 0; i < winNormalize.length; i++) {
  const [input, expect] = winNormalize[i]
  test(`path.win32.normalize extra ${i + 1}`, (t) => {
    t.equal(path.win32.normalize(input), expect)
  })
}

const winRelative = [
  ['C:\\a\\b', 'C:\\a\\b', ''],
  ['C:\\a\\b', 'C:\\a\\b\\c', 'c'],
  ['C:\\a\\b\\c', 'C:\\a\\b', '..'],
  ['C:\\a\\b\\c', 'D:\\e', 'D:\\e'],
  ['\\\\server\\share\\a', '\\x\\y', '\\x\\y']
]
for (let i = 0; i < winRelative.length; i++) {
  const [from, to, expect] = winRelative[i]
  test(`path.win32.relative extra ${i + 1}`, (t) => {
    t.equal(path.win32.relative(from, to), expect)
  })
}

// isAbsolute checks for mixed inputs
const absInputs = [
  ['/', true],
  ['\\', true],
  ['C:\\', true],
  ['C:foo', false],
  ['.', false],
  ['..', false]
]
for (let i = 0; i < absInputs.length; i++) {
  const [p, expect] = absInputs[i]
  test(`path.isAbsolute extra ${i + 1}`, (t) => {
    t.equal(path.isAbsolute(p), expect)
  })
}
