import Buffer from 'oro:buffer'
import path from 'oro:path'
import fs from 'oro:fs/promises'
import os from 'oro:os'

import { FileHandle } from 'oro:fs/handle'
import { test } from 'oro:test'
import { Dir } from 'oro:fs/dir'
import FIXTURES from '../fixtures.js'

const TMPDIR = `${os.tmpdir()}${path.sep}`

test('fs.promises.access', async (t) => {
  const { F_OK, R_OK, W_OK, X_OK } = fs.constants

  let access = await fs.access(FIXTURES, F_OK)
  t.equal(access, true, '(F_OK) fixtures/ directory is accessible')

  access = await fs.access(FIXTURES, R_OK)
  t.equal(access, true, '(F_OK | R_OK) fixtures/ directory is readable')

  access = await fs.access('.', W_OK)
  t.equal(access, true, '(W_OK) ./ directory is writable')

  access = await fs.access(FIXTURES, X_OK)
  t.equal(
    access,
    true,
    '(X_OK) fixtures/ directory is "executable" - can list items'
  )
})

if (os.platform() !== 'android') {
  test('fs.promises.chmod', async (t) => {
    const chmod = await fs.chmod(FIXTURES + 'file.txt', 0o777)
    t.equal(chmod, undefined, 'file.txt is chmod 777')
  })

  test('fs.promises.mkdir', async (t) => {
    const dirname = FIXTURES + Math.random().toString(16).slice(2)
    await fs.mkdir(dirname, {})
    await fs.stat(dirname)
    t.pass("fs.promises.mkdir made a directory and stat'd it")
  })

  test('fs.promises.mkdir throws on existing dir', async (t) => {
    const dirname = FIXTURES + Math.random().toString(16).slice(2)
    await fs.mkdir(dirname, {})
    await fs.stat(dirname)
    t.pass("fs.promises.mkdir made a directory and stat'd it")
    try {
      await fs.mkdir(dirname, {})
      t.fail("The second mkdir should throw, and this assertion shouldn't run")
    } catch {
      t.pass('remaking an existing directory, in non recursive mode throws')
    }
  })

  test('fs.promises.mkdir recursive', async (t) => {
    const randomDirName = () => Math.random().toString(16).slice(2)
    const dirname = path.join(
      FIXTURES,
      randomDirName(),
      randomDirName(),
      randomDirName()
    )
    await fs.mkdir(dirname, { recursive: true })
    await fs.stat(dirname)
    t.pass(
      "fs.promises.mkdir recursive made a few directories and stat'd the last one"
    )
  })

  test('fs.promises.mkdir recursive existing dir', async (t) => {
    const randomDirName = () => Math.random().toString(16).slice(2)
    const dirname = path.join(
      FIXTURES,
      randomDirName(),
      randomDirName(),
      randomDirName()
    )
    await fs.mkdir(dirname, { recursive: true })
    await fs.stat(dirname)
    t.pass(
      "fs.promises.mkdir recursive made a few directories and stat'd the last one"
    )
    try {
      await fs.mkdir(dirname, { recursive: true })
      t.pass('remaking a recirsive dir should not throw')
    } catch (err) {
      t.ifError(
        err,
        'When remaking the same directory, no error should be thrown'
      )
    }
  })
}

test('fs.promises.truncate/appendFile/rm/cp', async (t) => {
  const base = TMPDIR
  const srcDir = base + 'cp-src-' + Math.random().toString(16).slice(2)
  const dstDir = base + 'cp-dst-' + Math.random().toString(16).slice(2)
  await fs.mkdir(srcDir, { recursive: true })
  await fs.writeFile(srcDir + '/a.txt', 'hello')
  await fs.writeFile(srcDir + '/b.txt', '12345')
  await fs.truncate(srcDir + '/b.txt', 2)
  let buf = await fs.readFile(srcDir + '/b.txt')
  t.equal(buf.toString(), '12', 'truncate works')
  await fs.appendFile(srcDir + '/a.txt', '!')
  buf = await fs.readFile(srcDir + '/a.txt')
  t.equal(buf.toString(), 'hello!', 'appendFile works')
  await fs.cp(srcDir, dstDir, {
    recursive: true,
    preserveTimestamps: true,
    filter: (s) => !s.endsWith('/b.txt')
  })
  t.equal(
    (await fs.readFile(dstDir + '/a.txt')).toString(),
    'hello!',
    'cp a.txt ok'
  )
  try {
    await fs.readFile(dstDir + '/b.txt')
    t.fail('b.txt should have been filtered out')
  } catch {
    t.pass('filter skipped b.txt')
  }

  // errorOnExist behavior
  await fs.writeFile(base + 'cp-exists.txt', 'x')
  await fs.writeFile(base + 'cp-exists-dest.txt', 'y')
  try {
    await fs.cp(base + 'cp-exists.txt', base + 'cp-exists-dest.txt', {
      errorOnExist: true
    })
    t.fail('should error with errorOnExist')
  } catch {
    t.pass('errorOnExist triggers error when dest exists')
  }
  await fs.cp(base + 'cp-exists.txt', base + 'cp-exists-dest.txt', {
    errorOnExist: false
  })
  await fs.cp(base + 'cp-exists.txt', base + 'cp-exists-dest.txt', {
    force: true
  })
  await fs.rm(base + 'cp-exists.txt', { force: true })
  await fs.rm(base + 'cp-exists-dest.txt', { force: true })
  await fs.rm(dstDir, { recursive: true })
  try {
    await fs.stat(dstDir)
    t.fail('dstDir should be removed')
  } catch {
    t.pass('dstDir removed')
  }
  await fs.rm(srcDir, { recursive: true, force: true })

  // preserveMode (POSIX only)
  if (os.platform() !== 'win32') {
    const src =
      base + 'cp-mode-src-' + Math.random().toString(16).slice(2) + '.txt'
    const dst =
      base + 'cp-mode-dst-' + Math.random().toString(16).slice(2) + '.txt'
    await fs.writeFile(src, 'z')
    try {
      await (await import('oro:fs/promises')).chmod(src, 0o744)
    } catch {}
    await fs.cp(src, dst, { preserveMode: true })
    try {
      const m = (await fs.stat(dst)).mode & 0o777
      t.equal(m, 0o744, 'preserveMode applied')
    } catch {
      t.comment('preserveMode may be restricted; skipping')
    }
    await fs.rm(src, { force: true })
    await fs.rm(dst, { force: true })
  }

  // preserveOwner (POSIX only)
  if (os.platform() !== 'win32') {
    const src =
      base + 'cp-owner-src-' + Math.random().toString(16).slice(2) + '.txt'
    const dst =
      base + 'cp-owner-dst-' + Math.random().toString(16).slice(2) + '.txt'
    await fs.writeFile(src, 'owner')
    const srcStat = await fs.stat(src)
    await fs.cp(src, dst, { preserveOwner: true })
    const dstStat = await fs.stat(dst)
    t.equal(dstStat.uid, srcStat.uid, 'preserveOwner kept uid')
    t.equal(dstStat.gid, srcStat.gid, 'preserveOwner kept gid')
    await fs.rm(src, { force: true })
    await fs.rm(dst, { force: true })
  }
})

test('fs.promises.mkdtemp', async (t) => {
  const prefix = TMPDIR + 'oro-test-mkdtemp-'
  const dir = await fs.mkdtemp(prefix)
  t.ok((await fs.stat(dir)).isDirectory(), 'mkdtemp created a directory')
  await fs.rm(dir, { recursive: true, force: true })
})

if (os.platform() !== 'android') {
  test('FileHandle.writev/readv', async (t) => {
    const file = TMPDIR + 'vec-' + Math.random().toString(16).slice(2) + '.bin'
    const fh = await fs.open(file, 'w+')
    const a = Buffer.from('hello ')
    const b = Buffer.from('world')
    const c = Buffer.from('!')
    const totalWritten = await fh.writev([a, b, c], 0)
    t.equal(totalWritten, a.length + b.length + c.length, 'writev wrote all')
    await fh.datasync()
    await fh.close()

    const content = await fs.readFile(file)
    t.equal(content.toString(), 'hello world!', 'file content matches')

    const fh2 = await fs.open(file, 'r')
    const b1 = Buffer.alloc(6)
    const b2 = Buffer.alloc(6)
    const { bytesRead } = await fh2.readv([b1, b2], 0)
    t.equal(bytesRead, content.length, 'readv read full length')
    t.equal(
      Buffer.concat([b1, b2]).slice(0, bytesRead).toString(),
      'hello world!',
      'readv buffers match'
    )
    await fh2.close()
    await fs.rm(file, { force: true })
  })

  test('fs.promises.cp symlink with dereference=false', async (t) => {
    const target = FIXTURES + 'file.txt'
    const link = TMPDIR + 'cp-link-src-' + Math.random().toString(16).slice(2)
    const dest = TMPDIR + 'cp-link-dest-' + Math.random().toString(16).slice(2)
    try {
      await (await import('oro:fs/promises')).symlink(target, link)
    } catch {
      t.comment('symlink policy blocks test; skipping')
      return
    }
    try {
      await fs.cp(link, dest, { dereference: false })
      const rl = await fs.readlink(dest)
      t.ok(typeof rl === 'string' || Buffer.isBuffer(rl), 'readlink returns')
    } finally {
      try {
        await fs.rm(link, { force: true })
      } catch {}
      try {
        await fs.rm(dest, { force: true })
      } catch {}
    }
  })
}

test('fs.promises.open', async (t) => {
  const fd = await fs.open(FIXTURES + 'file.txt', 'r')
  t.ok(fd instanceof FileHandle, 'FileHandle is returned')
  await fd.close()
})

test('fs.promises.opendir', async (t) => {
  const dir = await fs.opendir(FIXTURES + 'directory')
  t.ok(dir instanceof Dir, 'fs.Dir is returned')
  await dir.close()
})

test('fs.promises.readdir', async (t) => {
  const files = await fs.readdir(FIXTURES + 'directory', {
    withFileTypes: true
  })
  t.ok(Array.isArray(files), 'array is returned')
  t.equal(files.length, 6, 'array contains 2 items')
  t.deepEqual(
    files.map((file) => file.name),
    ['0', '1', '2', 'a', 'b', 'c'].map((name) => `${name}.txt`),
    'array contains files'
  )
})

test('fs.promises.readdir with encoding buffer', async (t) => {
  const entries = await fs.readdir(FIXTURES + 'directory', {
    encoding: 'buffer'
  })
  t.ok(
    Array.isArray(entries) && Buffer.isBuffer(entries[0]),
    'readdir with encoding buffer returns Buffer[]'
  )
})

test('fs.promises.readlink encoding buffer', async (t) => {
  const link = FIXTURES + 'symlink-promises-buf.txt'
  const target = FIXTURES + 'file.txt'
  try {
    await fs.symlink(target, link)
  } catch {
    t.comment(
      'symlink not allowed by policy; skipping promises readlink buffer test'
    )
    return
  }
  try {
    const bufOrString = await fs.readlink(link, 'buffer')
    t.ok(
      Buffer.isBuffer(bufOrString),
      'promises readlink returns Buffer when encoding is "buffer"'
    )
  } finally {
    try {
      await fs.unlink(link)
    } catch {}
  }
})

test('fs.promises.readFile', async (t) => {
  const data = await fs.readFile(FIXTURES + 'file.txt')
  t.ok(Buffer.isBuffer(data), 'buffer is returned')
  t.equal(
    data.slice(0, 8).toString(),
    'test 123',
    'buffer contains file contents'
  )
})

test('fs.promises.stat', async (t) => {
  let stats = await fs.stat(FIXTURES + 'file.txt')
  t.ok(stats, 'stats are returned')
  t.equal(stats.isFile(), true, 'stats are for a file')
  t.equal(stats.isDirectory(), false, 'stats are not for a directory')
  t.equal(stats.isSymbolicLink(), false, 'stats are not for a symbolic link')
  t.equal(stats.isSocket(), false, 'stats are not for a socket')
  t.equal(stats.isFIFO(), false, 'stats are not for a FIFO')
  t.equal(stats.isBlockDevice(), false, 'stats are not for a block device')
  t.equal(
    stats.isCharacterDevice(),
    false,
    'stats are not for a character device'
  )

  stats = await fs.stat(FIXTURES + 'directory')
  t.ok(stats, 'stats are returned')
  t.equal(stats.isFile(), false, 'stats are not for a file')
  t.equal(stats.isDirectory(), true, 'stats are for a directory')
  t.equal(stats.isSymbolicLink(), false, 'stats are not for a symbolic link')
  t.equal(stats.isSocket(), false, 'stats are not for a socket')
  t.equal(stats.isFIFO(), false, 'stats are not for a FIFO')
  t.equal(stats.isBlockDevice(), false, 'stats are not for a block device')
  t.equal(
    stats.isCharacterDevice(),
    false,
    'stats are not for a character device'
  )
})

if (os.platform() !== 'android') {
  test('fs.promises.writeFile', async (t) => {
    const file = FIXTURES + 'write-file.txt'
    const data = 'test 123\n'
    await fs.writeFile(file, data)
    const contents = await fs.readFile(file)
    t.equal(contents.toString(), data, 'file contents are correct')
  })
}
