import test from 'oro:test'
import * as tar from 'oro:tar'
import { Buffer } from 'oro:buffer'
import fs from 'oro:fs'
import os from 'oro:os'
import path from 'oro:path'

const TMPDIR = `${os.tmpdir()}${path.sep}`

function randomArchivePath (prefix = 'oro-tar') {
  const id = Math.random().toString(16).slice(2)
  return `${TMPDIR}${prefix}-${id}.tar`
}

function padToBlock (buf, blockSize = 512) {
  const padding =
    buf.length % blockSize ? blockSize - (buf.length % blockSize) : 0
  if (!padding) return buf
  return Buffer.concat([buf, Buffer.alloc(padding)])
}

function writeOctal (buf, offset, width, value) {
  const digits = value.toString(8).padStart(width - 1, '0')
  buf.write(digits, offset, width - 1, 'ascii')
  buf[offset + width - 1] = 0
}

function writeSpaceOctal (buf, offset, width, value) {
  const digits = value.toString(8)
  if (digits.length > width - 1) {
    throw new RangeError('octal value does not fit in field')
  }
  const padding = ' '.repeat(width - 1 - digits.length)
  buf.write(padding + digits, offset, width - 1, 'ascii')
  buf[offset + width - 1] = 0
}

function writeBase256 (buf, offset, width, value) {
  let v = BigInt(value)
  const field = Buffer.alloc(width)
  for (let i = width - 1; i >= 0; i--) {
    field[i] = Number(v & 0xffn)
    v >>= 8n
  }
  field[0] |= 0x80
  field.copy(buf, offset)
}

function setChecksum (header) {
  header.fill(0x20, 148, 156)
  let sum = 0
  for (const byte of header) sum += byte

  const chk = sum.toString(8).padStart(6, '0')
  header.write(chk, 148, 6, 'ascii')
  header[154] = 0
  header[155] = 0x20
}

function setSignedChecksum (header) {
  header.fill(0x20, 148, 156)
  let sum = 0

  for (let i = 0; i < header.length; i++) {
    if (i >= 148 && i < 156) {
      sum += 0x20
      continue
    }

    const byte = header[i]
    sum += byte > 127 ? byte - 256 : byte
  }

  const chk = sum.toString(8).padStart(6, '0')
  header.write(chk, 148, 6, 'ascii')
  header[154] = 0
  header[155] = 0x20
}

function tarHeader (options) {
  const header = Buffer.alloc(512)

  const name = String(options.name || '')
  const type = (options.type || '0').toString()[0] || '0'
  const linkname = options.linkname ? String(options.linkname) : ''
  const mode = Number(options.mode ?? 0o644)
  const uid = Number(options.uid ?? 0)
  const gid = Number(options.gid ?? 0)
  const size = Number(options.size ?? 0)
  const mtime = Number(options.mtime ?? 0)

  header.write(name.slice(0, 100), 0, 'utf8')
  writeOctal(header, 100, 8, mode)
  writeOctal(header, 108, 8, uid)
  writeOctal(header, 116, 8, gid)
  writeOctal(header, 124, 12, size)
  writeOctal(header, 136, 12, mtime)

  header[156] = type.charCodeAt(0)

  if (linkname) {
    header.write(linkname.slice(0, 100), 157, 'utf8')
  }

  header.write('ustar', 257, 'ascii')
  header[262] = 0
  header.write('00', 263, 'ascii')

  setChecksum(header)

  return header
}

function paxRecord (key, value) {
  const body = `${key}=${value}\n`
  let len = body.length + 3
  while (true) {
    const lenStr = String(len)
    const next = lenStr.length + 1 + body.length
    if (next === len) {
      return `${lenStr} ${body}`
    }
    len = next
  }
}

function paxHeader (payload, name = './PaxHeaders') {
  const buf = Buffer.from(String(payload), 'utf8')
  return Buffer.concat([
    tarHeader({ name, type: 'x', size: buf.length, mode: 0o644 }),
    padToBlock(buf)
  ])
}

function paxSparseArchiveV00 (name, realsize, regions) {
  const sparse = Array.isArray(regions) ? regions : []
  const map = sparse
    .map((r) => [Number(r.offset ?? 0), Number(r.length ?? 0)])
    .concat([[Number(realsize), 0]])

  const payload = [
    paxRecord('GNU.sparse.size', String(realsize)),
    paxRecord('GNU.sparse.numblocks', String(map.length)),
    ...map.flatMap(([offset, length]) => [
      paxRecord('GNU.sparse.offset', String(offset)),
      paxRecord('GNU.sparse.numbytes', String(length))
    ])
  ].join('')

  const bodies = sparse.map((r) =>
    Buffer.alloc(Number(r.length ?? 0), r.fill ?? 0)
  )
  const storedSize = bodies.reduce((n, b) => n + b.length, 0)

  return Buffer.concat([
    paxHeader(payload, `./PaxHeaders/${name}`),
    tarHeader({ name, type: '0', size: storedSize, mode: 0o644 }),
    padToBlock(Buffer.concat(bodies)),
    Buffer.alloc(1024)
  ])
}

function paxSparseArchiveV01 (entryName, realsize, regions) {
  const sparse = Array.isArray(regions) ? regions : []
  const map = sparse
    .map((r) => [Number(r.offset ?? 0), Number(r.length ?? 0)])
    .concat([[Number(realsize), 0]])

  const mapValue = map
    .map(([offset, length]) => `${offset},${length}`)
    .join(',')

  const payload = [
    paxRecord('GNU.sparse.size', String(realsize)),
    paxRecord('GNU.sparse.numblocks', String(map.length)),
    paxRecord('GNU.sparse.name', String(entryName)),
    paxRecord('GNU.sparse.map', mapValue)
  ].join('')

  const bodies = sparse.map((r) =>
    Buffer.alloc(Number(r.length ?? 0), r.fill ?? 0)
  )
  const storedSize = bodies.reduce((n, b) => n + b.length, 0)

  const headerName = `./GNUSparseFile.1234567/${entryName}`

  return Buffer.concat([
    paxHeader(payload, `./PaxHeaders/${entryName}`),
    tarHeader({ name: headerName, type: '0', size: storedSize, mode: 0o644 }),
    padToBlock(Buffer.concat(bodies)),
    Buffer.alloc(1024)
  ])
}

function paxSparseArchiveV10 (entryName, realsize, regions) {
  const sparse = Array.isArray(regions) ? regions : []
  const map = sparse
    .map((r) => [Number(r.offset ?? 0), Number(r.length ?? 0)])
    .concat([[Number(realsize), 0]])

  const payload = [
    paxRecord('GNU.sparse.major', '1'),
    paxRecord('GNU.sparse.minor', '0'),
    paxRecord('GNU.sparse.name', String(entryName)),
    paxRecord('GNU.sparse.realsize', String(realsize))
  ].join('')

  const mapText = `${map.length}\n${map.flat().join('\n')}\n`
  const mapBuf = padToBlock(Buffer.from(mapText, 'utf8'))

  const bodies = sparse.map((r) =>
    Buffer.alloc(Number(r.length ?? 0), r.fill ?? 0)
  )
  const storedData = Buffer.concat([mapBuf, ...bodies])

  const headerName = `./GNUSparseFile.1234567/${entryName}`

  return Buffer.concat([
    paxHeader(payload, `./PaxHeaders/${entryName}`),
    tarHeader({
      name: headerName,
      type: '0',
      size: storedData.length,
      mode: 0o644
    }),
    padToBlock(storedData),
    Buffer.alloc(1024)
  ])
}

function gnuSparseHeader (options) {
  const header = Buffer.alloc(512)

  const name = String(options.name || '')
  const mode = Number(options.mode ?? 0o644)
  const uid = Number(options.uid ?? 0)
  const gid = Number(options.gid ?? 0)
  const storedSize = Number(options.storedSize ?? 0)
  const mtime = Number(options.mtime ?? 0)
  const realsize = Number(options.realsize ?? 0)
  const regions = Array.isArray(options.regions) ? options.regions : []
  const isExtended = Boolean(options.isExtended)

  header.write(name.slice(0, 100), 0, 'utf8')
  writeOctal(header, 100, 8, mode)
  writeOctal(header, 108, 8, uid)
  writeOctal(header, 116, 8, gid)
  writeOctal(header, 124, 12, storedSize)
  writeOctal(header, 136, 12, mtime)

  header[156] = 'S'.charCodeAt(0)

  // GNU tar uses "ustar " for sparse headers, but the reader only requires
  // a valid checksum. We keep the GNU magic for fidelity.
  header.write('ustar ', 257, 'ascii')
  header[263] = 0x20
  header[264] = 0x00

  // Sparse map (old GNU format):
  // - 4 entries in the main header at offset 386 (each 24 bytes)
  // - isextended at offset 482
  // - realsize at offset 483
  const base = 386
  const pairs = Math.min(4, regions.length)
  for (let i = 0; i < pairs; i++) {
    const region = regions[i]
    writeOctal(header, base + i * 24, 12, Number(region.offset ?? 0))
    writeOctal(header, base + i * 24 + 12, 12, Number(region.length ?? 0))
  }

  if (!isExtended && pairs < 4) {
    writeOctal(header, base + pairs * 24, 12, realsize)
    writeOctal(header, base + pairs * 24 + 12, 12, 0)
  }

  header[482] = isExtended ? 1 : 0
  writeOctal(header, 483, 12, realsize)

  header.fill(0x20, 148, 156)
  let sum = 0
  for (const byte of header) sum += byte

  const chk = sum.toString(8).padStart(6, '0')
  header.write(chk, 148, 6, 'ascii')
  header[154] = 0
  header[155] = 0x20

  return header
}

function gnuSparseExtHeader (regions, realsize, isExtended = false) {
  const block = Buffer.alloc(512)
  const pairs = Math.min(21, regions.length)
  for (let i = 0; i < pairs; i++) {
    const region = regions[i]
    writeOctal(block, i * 24, 12, Number(region.offset ?? 0))
    writeOctal(block, i * 24 + 12, 12, Number(region.length ?? 0))
  }

  const terminatorIndex = pairs
  if (terminatorIndex < 21) {
    writeOctal(block, terminatorIndex * 24, 12, realsize)
    writeOctal(block, terminatorIndex * 24 + 12, 12, 0)
  }

  block[504] = isExtended ? 1 : 0
  return block
}

function gnuSparseArchive (name, realsize, regions) {
  const storedSize = regions.reduce((n, r) => n + Number(r.length ?? 0), 0)
  const isExtended = regions.length > 4

  const header = gnuSparseHeader({
    name,
    storedSize,
    mtime: 0,
    realsize,
    regions,
    isExtended
  })

  const payloadParts = []

  if (isExtended) {
    payloadParts.push(gnuSparseExtHeader(regions.slice(4), realsize))
  }

  for (const region of regions) {
    const fill = region.fill ?? 0
    payloadParts.push(Buffer.alloc(Number(region.length ?? 0), fill))
  }

  const payload = padToBlock(Buffer.concat(payloadParts))

  return Buffer.concat([header, payload, Buffer.alloc(1024)])
}

async function createSampleArchive (_t) {
  const archivePath = randomArchivePath()
  const archive = await tar.create(archivePath, { writable: true, mmap: true })

  try {
    // simple text entry
    const textBody = Buffer.from('hello world')
    await archive.append(
      {
        path: 'foo.txt',
        mode: 0o644
      },
      textBody
    )

    // streamed entry using async iterable
    const chunks = [Buffer.from('chunk-1-'), Buffer.from('chunk-2')]
    const totalSize = chunks.reduce((n, c) => n + c.length, 0)

    async function * streamBody () {
      for (const chunk of chunks) {
        yield chunk
      }
    }

    await archive.append(
      {
        path: 'dir/streamed.txt',
        size: totalSize,
        mode: 0o644
      },
      streamBody()
    )

    await archive.finalize()
    await archive.close()
  } catch (err) {
    // ensure archive is closed on failure
    try {
      await archive.close()
    } catch {}
    throw err
  }

  return archivePath
}

test('tar: create, append, finalize, reopen and read', async (t) => {
  const archivePath = await createSampleArchive(t)

  const archive = await tar.open(archivePath, { mmap: true })
  t.ok(archive instanceof tar.TarArchive, 'tar.open returns TarArchive')
  t.equal(archive.writable, false, 'reopened archive is read-only')

  const entries = await archive.entries()
  t.ok(Array.isArray(entries), 'entries() returns an array')
  t.ok(entries.length >= 2, 'archive has at least 2 entries')

  const names = entries.map((e) => e.path).sort()
  t.ok(names.includes('foo.txt'), 'entries() includes foo.txt')
  t.ok(names.includes('dir/streamed.txt'), 'entries() includes streamed entry')

  const statFoo = await archive.stat('foo.txt')
  t.equal(statFoo.path, 'foo.txt', 'stat() returns correct path')
  t.equal(statFoo.kind, 'file', 'stat() returns kind=file')
  t.ok(statFoo.size > 0, 'stat() size > 0')

  const full = await archive.read('foo.txt')
  t.equal(full.toString(), 'hello world', 'read() returns full entry body')

  const sliced = await archive.read('foo.txt', { offset: 6, length: 5 })
  t.equal(sliced.toString(), 'world', 'read() respects offset/length')

  // streaming read
  const chunks = []
  for await (const buf of archive.readStream('dir/streamed.txt', {
    highWaterMark: 4
  })) {
    chunks.push(buf)
  }
  t.equal(
    Buffer.concat(chunks).toString(),
    'chunk-1-chunk-2',
    'readStream() yields complete streamed entry'
  )

  await archive.close()
  await fs.promises.rm(archivePath, { force: true })
})

test('tar: static TarArchive.open/create vs top-level helpers', async (t) => {
  const archivePath = await createSampleArchive(t)

  const fromHelper = await tar.open(archivePath)
  const fromStatic = await tar.TarArchive.open(archivePath)

  const [entriesHelper, entriesStatic] = await Promise.all([
    fromHelper.entries(),
    fromStatic.entries()
  ])

  t.equal(
    entriesHelper.length,
    entriesStatic.length,
    'TarArchive.open and tar.open see same number of entries'
  )

  await fromHelper.close()
  await fromStatic.close()
  await fs.promises.rm(archivePath, { force: true })
})

test('tar: readStream supports start/end and highWaterMark', async (t) => {
  const archivePath = randomArchivePath()
  const archive = await tar.create(archivePath, { writable: true })

  const body = Buffer.from('abcde')
  await archive.append({ path: 'letters.txt' }, body)
  await archive.finalize()
  await archive.close()

  const reopened = await tar.open(archivePath)
  const chunks = []
  for await (const buf of reopened.readStream('letters.txt', {
    start: 1,
    end: 3,
    highWaterMark: 1
  })) {
    chunks.push(buf)
  }

  t.equal(
    Buffer.concat(chunks).toString(),
    'bcd',
    'readStream respects start/end and highWaterMark'
  )

  await reopened.close()
  await fs.promises.rm(archivePath, { force: true })
})

test('tar: append enforces header.size for async iterables', async (t) => {
  const archivePath = randomArchivePath()
  const archive = await tar.create(archivePath, { writable: true })

  async function * body () {
    yield Buffer.from('foo')
  }

  await t.rejects(
    archive.append({ path: 'x.txt' }, body()),
    /header\.size/,
    'async iterable without header.size is rejected'
  )

  await archive.finalize()
  await archive.close()
  await fs.promises.rm(archivePath, { force: true })
})

test('tar: append rejects when header.size mismatches buffer length', async (t) => {
  const archivePath = randomArchivePath()
  const archive = await tar.create(archivePath, { writable: true })

  const body = Buffer.from('hello world')

  await t.rejects(
    archive.append({ path: 'x.txt', size: body.length + 1 }, body),
    /header\.size does not match body length/,
    'append rejects when header.size does not match body length'
  )

  await archive.finalize()
  await archive.close()
  await fs.promises.rm(archivePath, { force: true })
})

test('tar: append enforces async iterable size accounting (overflow)', async (t) => {
  const archivePath = randomArchivePath()
  const archive = await tar.create(archivePath, { writable: true })

  async function * body () {
    yield Buffer.from('hello')
    yield Buffer.from('world')
  }

  const size = Buffer.from('hello').length

  await t.rejects(
    archive.append({ path: 'overflow.txt', size }, body()),
    /body yielded more data than header\.size allows/,
    'async iterable that yields more bytes than header.size is rejected'
  )

  await archive.close()
  await fs.promises.rm(archivePath, { force: true })
})

test('tar: append enforces async iterable size accounting (underflow)', async (t) => {
  const archivePath = randomArchivePath()
  const archive = await tar.create(archivePath, { writable: true })

  async function * body () {
    yield Buffer.from('hello')
  }

  const size = Buffer.from('hello').length + 5

  await t.rejects(
    archive.append({ path: 'underflow.txt', size }, body()),
    /body ended before header\.size bytes were yielded/,
    'async iterable that yields fewer bytes than header.size is rejected'
  )

  await archive.close()
  await fs.promises.rm(archivePath, { force: true })
})

test('tar: read parameter validation', async (t) => {
  const archivePath = await createSampleArchive(t)
  const archive = await tar.open(archivePath)

  await t.rejects(
    archive.read('foo.txt', { offset: -1 }),
    /offset must be a non-negative finite number/,
    'negative offset is rejected'
  )

  await t.rejects(
    archive.read('foo.txt', { length: -5 }),
    /length must be a non-negative finite number/,
    'negative length is rejected'
  )

  await t.rejects(
    archive.read('missing.txt'),
    /Entry not found/i,
    'reading missing entry is rejected'
  )

  await archive.close()
  await fs.promises.rm(archivePath, { force: true })
})

test('tar: directory entries report correct kind', async (t) => {
  const archivePath = randomArchivePath()
  const archive = await tar.create(archivePath, { writable: true })

  // directory entry with no body
  await archive.append(
    {
      path: 'dir-only/',
      kind: 'directory'
    },
    null
  )
  await archive.finalize()
  await archive.close()

  const reopened = await tar.open(archivePath)
  const stats = await reopened.entries()
  const dir = stats.find((e) => e.path === 'dir-only/')

  t.ok(dir, 'directory entry is present in entries()')
  t.equal(dir.kind, 'directory', 'directory entry kind is directory')
  t.equal(dir.isDirectory, true, 'isDirectory is true for directory entry')

  await t.rejects(
    reopened.read('dir-only/'),
    /EISDIR|directory entry/i,
    'read() rejects for directory entries'
  )

  await reopened.close()
  await fs.promises.rm(archivePath, { force: true })
})

test('tar: operations after close are rejected', async (t) => {
  const archivePath = await createSampleArchive(t)
  const archive = await tar.open(archivePath)

  await archive.close()

  await t.rejects(
    archive.entries(),
    /TarArchive is closed/,
    'entries() rejects after archive is closed'
  )

  await t.rejects(
    archive.read('foo.txt'),
    /TarArchive is closed/,
    'read() rejects after archive is closed'
  )

  await fs.promises.rm(archivePath, { force: true })
})

test('tar: append after finalize is rejected', async (t) => {
  const archivePath = randomArchivePath()
  const archive = await tar.create(archivePath, { writable: true })

  await archive.append({ path: 'foo.txt' }, Buffer.from('hello'))
  await archive.finalize()

  await t.rejects(
    archive.append({ path: 'bar.txt' }, Buffer.from('world')),
    /TarArchive has been finalized/,
    'append() rejects after archive has been finalized'
  )

  await archive.close()
  await fs.promises.rm(archivePath, { force: true })
})

test('tar: fromBuffer opens in-memory archive', async (t) => {
  const archivePath = await createSampleArchive(t)
  const raw = await fs.promises.readFile(archivePath)

  const archive = await tar.fromBuffer(raw)
  const entries = await archive.entries()
  t.ok(entries.length >= 2, 'fromBuffer sees entries from in-memory tar')

  const full = await archive.read('foo.txt')
  t.equal(full.toString(), 'hello world', 'fromBuffer read() works')

  await archive.close()
  await fs.promises.rm(archivePath, { force: true })
})

test('tar: fromBuffer accepts Uint8Array and ArrayBuffer', async (t) => {
  const archivePath = await createSampleArchive(t)
  const raw = await fs.promises.readFile(archivePath)

  const asUint8 = new Uint8Array(raw.buffer, raw.byteOffset, raw.byteLength)
  const asArrayBuffer = raw.buffer.slice(
    raw.byteOffset,
    raw.byteOffset + raw.byteLength
  )

  const archiveFromUint8 = await tar.fromBuffer(asUint8)
  const archiveFromArrayBuffer = await tar.fromBuffer(asArrayBuffer)

  const foo1 = await archiveFromUint8.read('foo.txt')
  const foo2 = await archiveFromArrayBuffer.read('foo.txt')

  t.equal(
    foo1.toString(),
    'hello world',
    'fromBuffer(Uint8Array) can read entries'
  )
  t.equal(
    foo2.toString(),
    'hello world',
    'fromBuffer(ArrayBuffer) can read entries'
  )

  await archiveFromUint8.close()
  await archiveFromArrayBuffer.close()
  await fs.promises.rm(archivePath, { force: true })
})

test('tar: in-memory writer round-trip via toBuffer and fromBuffer', async (t) => {
  const archive = await tar.createInMemory({
    uid: 1000,
    gid: 1000,
    uname: 'alice',
    gname: 'staff'
  })

  const payload = Buffer.from('in-memory tar payload')
  await archive.append({ path: 'mem.txt' }, payload)

  const buf = await archive.toBuffer()
  t.ok(Buffer.isBuffer(buf), 'toBuffer returns a Buffer')
  t.ok(buf.length > 0, 'buffer has non-zero length')

  const reopened = await tar.fromBuffer(buf)
  const entries = await reopened.entries()
  t.equal(entries.length, 1, 'reopened in-memory archive has one entry')
  t.equal(entries[0].path, 'mem.txt', 'entry path matches')

  const body = await reopened.read('mem.txt')
  t.equal(
    body.toString(),
    payload.toString(),
    'entry body matches original payload'
  )

  await archive.close()
  await reopened.close()
})

test('tar: in-memory writer supports read/stat after finalize', async (t) => {
  const archive = await tar.createInMemory()

  const payload = Buffer.from('hello from memory')
  await archive.append({ path: 'mem.txt' }, payload)
  await archive.finalize()

  const entries = await archive.entries()
  t.equal(entries.length, 1, 'finalized in-memory archive can list entries')
  t.equal(entries[0].path, 'mem.txt', 'entry path matches')

  const stat = await archive.stat('mem.txt')
  t.equal(stat.size, payload.length, 'stat() returns correct size')

  const buf = await archive.read('mem.txt')
  t.equal(buf.toString(), payload.toString(), 'read() returns entry body')

  await archive.close()
})

test('tar: extract writes entry to filesystem using streaming', async (t) => {
  const archivePath = await createSampleArchive(t)
  const archive = await tar.open(archivePath)

  const destDir = os.tmpdir()
  const destPath = path.join(
    destDir,
    `oro-tar-extract-${Math.random().toString(16).slice(2)}.txt`
  )

  await archive.extract('foo.txt', destPath)

  const content = await fs.promises.readFile(destPath, 'utf8')
  t.equal(
    content,
    'hello world',
    'extract() writes correct content to destination'
  )

  await archive.close()
  await fs.promises.rm(archivePath, { force: true })
  await fs.promises.rm(destPath, { force: true })
})

test('tar: append supports empty file bodies', async (t) => {
  const archivePath = randomArchivePath()
  const archive = await tar.create(archivePath, { writable: true })

  await archive.append({ path: 'empty.txt' }, Buffer.alloc(0))
  await archive.finalize()
  await archive.close()

  const reopened = await tar.open(archivePath)
  const stat = await reopened.stat('empty.txt')
  t.equal(stat.size, 0, 'empty file has size 0')

  const buf = await reopened.read('empty.txt')
  t.equal(buf.length, 0, 'reading empty file returns empty buffer')

  await reopened.close()
  await fs.promises.rm(archivePath, { force: true })
})

test('tar: extractAll writes all file entries to destination directory', async (t) => {
  const archivePath = await createSampleArchive(t)
  const archive = await tar.open(archivePath)

  const destDir = path.join(
    os.tmpdir(),
    `oro-tar-extract-all-${Math.random().toString(16).slice(2)}`
  )
  await archive.extractAll(destDir)

  const foo = await fs.promises.readFile(path.join(destDir, 'foo.txt'), 'utf8')
  const streamed = await fs.promises.readFile(
    path.join(destDir, 'dir', 'streamed.txt'),
    'utf8'
  )

  t.equal(foo, 'hello world', 'extractAll() writes foo.txt')
  t.equal(streamed, 'chunk-1-chunk-2', 'extractAll() writes streamed entry')

  await archive.close()
  await fs.promises.rm(archivePath, { force: true })
  await fs.promises.rm(destDir, { recursive: true, force: true })
})

test('tar: gnu longname entries and ./ path normalization', async (t) => {
  const longName = `./dir/${'a'.repeat(140)}.txt`
  const longNameBody = Buffer.from(`${longName}\0`)

  const longHeader = tarHeader({
    name: '././@LongLink',
    type: 'L',
    size: longNameBody.length,
    mode: 0o644
  })

  const payload = Buffer.from('hello')
  const fileHeader = tarHeader({
    name: 'ignored.txt',
    type: '0',
    size: payload.length,
    mode: 0o644
  })

  const raw = Buffer.concat([
    longHeader,
    padToBlock(longNameBody),
    fileHeader,
    padToBlock(payload),
    Buffer.alloc(1024) // two zero blocks
  ])

  const archive = await tar.fromBuffer(raw)
  const entries = await archive.entries()

  t.equal(entries.length, 1, 'longname header is not exposed as an entry')
  t.equal(entries[0].path, longName, 'longname path is applied to next entry')

  const buf = await archive.read(entries[0].path)
  t.equal(buf.toString(), 'hello', 'read() works with ./ prefixed entry paths')

  await archive.close()
})

test('tar: space-padded octal size fields are decoded', async (t) => {
  const payload = Buffer.from('hello')
  const header = tarHeader({
    name: 'space-size.bin',
    type: '0',
    size: payload.length,
    mode: 0o644
  })

  writeSpaceOctal(header, 124, 12, payload.length)
  setChecksum(header)

  const raw = Buffer.concat([header, padToBlock(payload), Buffer.alloc(1024)])
  const archive = await tar.fromBuffer(raw)

  const stat = await archive.stat('space-size.bin')
  t.equal(stat.size, payload.length, 'stat reports correct size')

  const buf = await archive.read('space-size.bin')
  t.equal(buf.toString(), 'hello', 'read() returns expected payload')

  await archive.close()
})

test('tar: base-256 size fields are decoded', async (t) => {
  const payload = Buffer.alloc(2048, 0x61)
  const header = tarHeader({
    name: 'base256.bin',
    type: '0',
    size: payload.length,
    mode: 0o644
  })

  writeBase256(header, 124, 12, payload.length)
  setChecksum(header)

  const raw = Buffer.concat([header, padToBlock(payload), Buffer.alloc(1024)])
  const archive = await tar.fromBuffer(raw)

  const stat = await archive.stat('base256.bin')
  t.equal(stat.size, payload.length, 'stat reports correct size')

  const buf = await archive.read('base256.bin')
  t.equal(buf.length, payload.length, 'read() returns full size')
  t.equal(buf[0], 0x61, 'payload begins with expected byte')
  t.equal(buf[buf.length - 1], 0x61, 'payload ends with expected byte')

  await archive.close()
})

test('tar: signed header checksums are accepted', async (t) => {
  const payload = Buffer.from('hello')
  const name = 'caf\u00e9.txt'
  const header = tarHeader({
    name,
    type: '0',
    size: payload.length,
    mode: 0o644
  })

  setSignedChecksum(header)

  const raw = Buffer.concat([header, padToBlock(payload), Buffer.alloc(1024)])
  const archive = await tar.fromBuffer(raw)
  const buf = await archive.read(name)
  t.equal(buf.toString(), 'hello', 'archive opens and reads payload')
  await archive.close()
})

test('tar: PAX extended headers tolerate trailing NUL padding', async (t) => {
  const entryName = 'pax-padded.txt'
  const paxPayload = paxRecord('path', entryName)
  const paxBuf = Buffer.from(paxPayload, 'utf8')
  const padded = Buffer.concat([paxBuf, Buffer.alloc(32)])

  const paxHeaderEntry = tarHeader({
    name: `./PaxHeaders/${entryName}`,
    type: 'x',
    size: padded.length,
    mode: 0o644
  })

  const body = Buffer.from('ok')
  const fileHeader = tarHeader({
    name: 'ignored.txt',
    type: '0',
    size: body.length,
    mode: 0o644
  })

  const raw = Buffer.concat([
    paxHeaderEntry,
    padToBlock(padded),
    fileHeader,
    padToBlock(body),
    Buffer.alloc(1024)
  ])

  const archive = await tar.fromBuffer(raw)
  const entries = await archive.entries()
  t.equal(entries.length, 1, 'pax header is not exposed as an entry')
  t.equal(entries[0].path, entryName, 'pax path is applied to next entry')

  const buf = await archive.read(entryName)
  t.equal(buf.toString(), 'ok', 'read() works with padded pax headers')

  await archive.close()
})

test('tar: writer encodes large uid/gid values', async (t) => {
  const archive = await tar.createInMemory({ uid: 5000000, gid: 5000001 })
  await archive.append({ path: 'uid.bin' }, Buffer.from('hi'))

  const buf = await archive.toBuffer()
  const reopened = await tar.fromBuffer(buf)

  const stat = await reopened.stat('uid.bin')
  t.equal(stat.uid, 5000000, 'uid round-trips')
  t.equal(stat.gid, 5000001, 'gid round-trips')

  await archive.close()
  await reopened.close()
})

test('tar: writer encodes sparse file entries (GNU PAX sparse v0.0 style)', async (t) => {
  const archive = await tar.createInMemory()

  const stored = Buffer.concat([
    Buffer.alloc(512, 0x41),
    Buffer.alloc(512, 0x42)
  ])

  await archive.append(
    {
      path: 'sparse-write.bin',
      size: stored.length,
      sparseSize: 4096,
      sparse: [
        { offset: 0, length: 512 },
        { offset: 3584, length: 512 }
      ]
    },
    stored
  )

  await archive.append(
    {
      path: 'sparse-hole.bin',
      size: 0,
      sparseSize: 2048,
      sparse: []
    },
    null
  )

  await archive.finalize()

  const buf = await archive.toBuffer()
  const reopened = await tar.fromBuffer(buf)

  const stat = await reopened.stat('sparse-write.bin')
  t.equal(stat.kind, 'file', 'sparse entry kind is file')
  t.equal(stat.size, 4096, 'sparse entry reports real size')
  t.ok(Array.isArray(stat.sparse), 'stat includes sparse map')
  t.equal(stat.sparse.length, 2, 'sparse map includes 2 data regions')
  t.equal(stat.sparse[0].offset, 0, 'region 1 offset matches')
  t.equal(stat.sparse[0].length, 512, 'region 1 length matches')
  t.equal(stat.sparse[1].offset, 3584, 'region 2 offset matches')
  t.equal(stat.sparse[1].length, 512, 'region 2 length matches')

  const content = await reopened.read('sparse-write.bin')
  t.equal(content.length, 4096, 'read returns full sparse file size')
  t.equal(content[0], 0x41, 'segment 1 begins at offset 0')
  t.equal(content[511], 0x41, 'segment 1 ends at offset 511')
  t.equal(content[512], 0x00, 'hole bytes are zero-filled')
  t.equal(content[3584], 0x42, 'segment 2 begins at offset 3584')
  t.equal(content[4095], 0x42, 'segment 2 ends at EOF')

  const holeStat = await reopened.stat('sparse-hole.bin')
  t.equal(holeStat.size, 2048, 'hole-only sparse entry reports real size')
  t.ok(Array.isArray(holeStat.sparse), 'hole-only entry includes sparse map')
  t.equal(holeStat.sparse.length, 0, 'hole-only sparse map is empty')

  const holeContent = await reopened.read('sparse-hole.bin')
  t.equal(holeContent.length, 2048, 'hole-only entry reads full size')
  t.equal(holeContent[0], 0x00, 'hole-only entry begins with zeros')
  t.equal(holeContent[2047], 0x00, 'hole-only entry ends with zeros')

  await archive.close()
  await reopened.close()
})

test('tar: gnu sparse entries are readable (no extensions)', async (t) => {
  const raw = gnuSparseArchive('sparse.bin', 4096, [
    { offset: 0, length: 512, fill: 0x41 },
    { offset: 3584, length: 512, fill: 0x42 }
  ])

  const archive = await tar.fromBuffer(raw)
  const stat = await archive.stat('sparse.bin')

  t.equal(stat.kind, 'file', 'sparse entry kind is file')
  t.equal(stat.size, 4096, 'sparse entry reports real size')

  const buf = await archive.read('sparse.bin')
  t.equal(buf.length, 4096, 'read() returns full sparse file size')

  t.equal(buf[0], 0x41, 'sparse file starts with segment data')
  t.equal(buf[511], 0x41, 'segment data spans first region')
  t.equal(buf[512], 0x00, 'hole bytes are zero-filled')
  t.equal(buf[3583], 0x00, 'hole bytes are zero-filled')
  t.equal(buf[3584], 0x42, 'second region begins at correct offset')
  t.equal(buf[4095], 0x42, 'sparse file ends with segment data')

  const slice = await archive.read('sparse.bin', { offset: 500, length: 50 })
  t.equal(slice.length, 50, 'read() slice returns requested length')
  t.equal(slice[0], 0x41, 'slice starts within first region')
  t.equal(slice[11], 0x41, 'slice includes end of first region')
  t.equal(slice[12], 0x00, 'slice crosses into a hole')

  await archive.close()
})

test('tar: gnu sparse entries are readable (extended header)', async (t) => {
  const raw = gnuSparseArchive('sparse-ext.bin', 4608, [
    { offset: 0, length: 512, fill: 0x41 },
    { offset: 1024, length: 512, fill: 0x42 },
    { offset: 2048, length: 512, fill: 0x43 },
    { offset: 3072, length: 512, fill: 0x44 },
    { offset: 4096, length: 512, fill: 0x45 }
  ])

  const archive = await tar.fromBuffer(raw)
  const stat = await archive.stat('sparse-ext.bin')
  t.equal(stat.kind, 'file', 'sparse entry kind is file')
  t.equal(stat.size, 4608, 'sparse entry reports real size')

  const buf = await archive.read('sparse-ext.bin')
  t.equal(buf.length, 4608, 'read() returns full sparse file size')

  t.equal(buf[0], 0x41, 'segment 1 starts at offset 0')
  t.equal(buf[512], 0x00, 'hole after segment 1 is zero-filled')
  t.equal(buf[1024], 0x42, 'segment 2 starts at offset 1024')
  t.equal(buf[1536], 0x00, 'hole after segment 2 is zero-filled')
  t.equal(buf[2048], 0x43, 'segment 3 starts at offset 2048')
  t.equal(buf[2560], 0x00, 'hole after segment 3 is zero-filled')
  t.equal(buf[3072], 0x44, 'segment 4 starts at offset 3072')
  t.equal(buf[3584], 0x00, 'hole after segment 4 is zero-filled')
  t.equal(buf[4096], 0x45, 'segment 5 starts at offset 4096')
  t.equal(buf[4607], 0x45, 'segment 5 ends at EOF')

  await archive.close()
})

test('tar: gnu sparse PAX v0.0 entries are readable', async (t) => {
  const raw = paxSparseArchiveV00('sparse-pax-0.0.bin', 4096, [
    { offset: 0, length: 512, fill: 0x41 },
    { offset: 3584, length: 512, fill: 0x42 }
  ])

  const archive = await tar.fromBuffer(raw)
  const stat = await archive.stat('sparse-pax-0.0.bin')
  t.equal(stat.kind, 'file', 'sparse entry kind is file')
  t.equal(stat.size, 4096, 'sparse entry reports real size')
  t.ok(Array.isArray(stat.sparse), 'stat includes sparse map')
  t.equal(stat.sparse.length, 2, 'sparse map includes 2 data regions')
  t.equal(stat.sparse[0].offset, 0, 'region 1 offset matches')
  t.equal(stat.sparse[0].length, 512, 'region 1 length matches')
  t.equal(stat.sparse[1].offset, 3584, 'region 2 offset matches')
  t.equal(stat.sparse[1].length, 512, 'region 2 length matches')

  const entries = await archive.entries()
  const listed = entries.find((e) => e.path === 'sparse-pax-0.0.bin')
  t.ok(listed, 'entries() includes sparse entry')
  t.ok(Array.isArray(listed.sparse), 'entries() includes sparse map')
  t.equal(listed.sparse.length, 2, 'entries() sparse map includes 2 regions')

  const buf = await archive.read('sparse-pax-0.0.bin')
  t.equal(buf.length, 4096, 'read() returns full sparse file size')
  t.equal(buf[0], 0x41, 'segment 1 starts at offset 0')
  t.equal(buf[511], 0x41, 'segment 1 ends at offset 511')
  t.equal(buf[512], 0x00, 'hole bytes are zero-filled')
  t.equal(buf[3584], 0x42, 'segment 2 starts at offset 3584')
  t.equal(buf[4095], 0x42, 'segment 2 ends at EOF')

  await archive.close()
})

test('tar: gnu sparse PAX v0.1 entries are readable', async (t) => {
  const raw = paxSparseArchiveV01('sparse-pax-0.1.bin', 4096, [
    { offset: 0, length: 512, fill: 0x43 },
    { offset: 3584, length: 512, fill: 0x44 }
  ])

  const archive = await tar.fromBuffer(raw)
  const stat = await archive.stat('sparse-pax-0.1.bin')
  t.equal(stat.kind, 'file', 'sparse entry kind is file')
  t.equal(stat.size, 4096, 'sparse entry reports real size')

  const buf = await archive.read('sparse-pax-0.1.bin')
  t.equal(buf.length, 4096, 'read() returns full sparse file size')
  t.equal(buf[0], 0x43, 'segment 1 starts at offset 0')
  t.equal(buf[511], 0x43, 'segment 1 ends at offset 511')
  t.equal(buf[512], 0x00, 'hole bytes are zero-filled')
  t.equal(buf[3584], 0x44, 'segment 2 starts at offset 3584')
  t.equal(buf[4095], 0x44, 'segment 2 ends at EOF')

  await archive.close()
})

test('tar: gnu sparse PAX v1.0 entries are readable', async (t) => {
  const raw = paxSparseArchiveV10('sparse-pax-1.0.bin', 4096, [
    { offset: 0, length: 512, fill: 0x45 },
    { offset: 3584, length: 512, fill: 0x46 }
  ])

  const archive = await tar.fromBuffer(raw)
  const stat = await archive.stat('sparse-pax-1.0.bin')
  t.equal(stat.kind, 'file', 'sparse entry kind is file')
  t.equal(stat.size, 4096, 'sparse entry reports real size')

  const buf = await archive.read('sparse-pax-1.0.bin')
  t.equal(buf.length, 4096, 'read() returns full sparse file size')
  t.equal(buf[0], 0x45, 'segment 1 starts at offset 0')
  t.equal(buf[511], 0x45, 'segment 1 ends at offset 511')
  t.equal(buf[512], 0x00, 'hole bytes are zero-filled')
  t.equal(buf[3584], 0x46, 'segment 2 starts at offset 3584')
  t.equal(buf[4095], 0x46, 'segment 2 ends at EOF')

  await archive.close()
})

test('tar: sparse entries with no stored data return zero-filled buffers', async (t) => {
  const raw = paxSparseArchiveV00('sparse-empty.bin', 2048, [])
  const archive = await tar.fromBuffer(raw)

  const stat = await archive.stat('sparse-empty.bin')
  t.equal(stat.size, 2048, 'stat reports real size')
  t.ok(Array.isArray(stat.sparse), 'stat includes sparse map')
  t.equal(stat.sparse.length, 0, 'sparse map is empty for hole-only files')

  const buf = await archive.read('sparse-empty.bin')
  t.equal(buf.length, 2048, 'read returns full size')
  t.equal(buf[0], 0x00, 'buffer begins with zeros')
  t.equal(buf[2047], 0x00, 'buffer ends with zeros')

  await archive.close()
})

test('tar: symlink and hardlink entries (read + extractAll preserveLinks)', async (t) => {
  const archive = await tar.createInMemory()

  await archive.append({ path: 'target.txt' }, Buffer.from('hello'))
  await archive.append(
    { path: 'sym.txt', kind: 'symlink', linkpath: 'target.txt' },
    null
  )
  await archive.append(
    { path: 'hard.txt', kind: 'hardlink', linkpath: 'target.txt' },
    null
  )
  await archive.finalize()

  const entries = await archive.entries()
  const sym = entries.find((e) => e.path === 'sym.txt')
  const hard = entries.find((e) => e.path === 'hard.txt')

  t.ok(sym, 'symlink entry is present')
  t.equal(sym.kind, 'symlink', 'symlink entry kind is symlink')
  t.equal(sym.linkpath, 'target.txt', 'symlink linkpath is preserved')

  t.ok(hard, 'hardlink entry is present')
  t.equal(hard.kind, 'hardlink', 'hardlink entry kind is hardlink')
  t.equal(hard.linkpath, 'target.txt', 'hardlink linkpath is preserved')

  const destDir = path.join(
    os.tmpdir(),
    `oro-tar-links-${Math.random().toString(16).slice(2)}`
  )
  await archive.extractAll(destDir, { preserveLinks: true })

  const targetPath = path.join(destDir, 'target.txt')
  const symPath = path.join(destDir, 'sym.txt')
  const hardPath = path.join(destDir, 'hard.txt')

  const content = await fs.promises.readFile(targetPath, 'utf8')
  t.equal(content, 'hello', 'target file extracted')

  const symTarget = await fs.promises.readlink(symPath)
  t.equal(symTarget, 'target.txt', 'symlink extracted')

  const hardContent = await fs.promises.readFile(hardPath, 'utf8')
  t.equal(hardContent, 'hello', 'hardlink extracted (content matches)')

  const statTarget = await fs.promises.stat(targetPath)
  const statHard = await fs.promises.stat(hardPath)
  if (typeof statTarget.ino === 'number' && typeof statHard.ino === 'number') {
    t.equal(statTarget.ino, statHard.ino, 'hardlink shares inode with target')
  }

  await archive.close()
  await fs.promises.rm(destDir, { recursive: true, force: true })
})

test('tar: extractAll refuses to traverse symlinks in destination tree', async (t) => {
  const archive = await tar.createInMemory()
  await archive.append({ path: 'linkdir/evil.txt' }, Buffer.from('pwn'))
  await archive.finalize()

  const destDir = path.join(
    os.tmpdir(),
    `oro-tar-symlink-traversal-${Math.random().toString(16).slice(2)}`
  )
  await fs.promises.mkdir(destDir, { recursive: true })

  try {
    await fs.promises.symlink(destDir, path.join(destDir, 'linkdir'))
  } catch (err) {
    t.skip(err.message || 'symlinks unavailable on this platform')
    await archive.close()
    await fs.promises.rm(destDir, { recursive: true, force: true })
    return
  }

  await t.rejects(
    archive.extractAll(destDir),
    /ELOOP|symlink/i,
    'extractAll rejects symlink traversal'
  )

  await archive.close()
  await fs.promises.rm(destDir, { recursive: true, force: true })
})

test('tar: extract refuses to traverse symlinks in destination tree', async (t) => {
  const archive = await tar.createInMemory()
  await archive.append({ path: 'linkdir/evil.txt' }, Buffer.from('pwn'))
  await archive.finalize()

  const destDir = path.join(
    os.tmpdir(),
    `oro-tar-extract-symlink-traversal-${Math.random().toString(16).slice(2)}`
  )
  await fs.promises.mkdir(destDir, { recursive: true })

  try {
    await fs.promises.symlink(destDir, path.join(destDir, 'linkdir'))
  } catch (err) {
    t.skip(err.message || 'symlinks unavailable on this platform')
    await archive.close()
    await fs.promises.rm(destDir, { recursive: true, force: true })
    return
  }

  const destPath = path.join(destDir, 'linkdir', 'evil.txt')
  await t.rejects(
    archive.extract('linkdir/evil.txt', destPath),
    /ELOOP|symlink/i,
    'extract rejects symlink traversal'
  )

  await archive.close()
  await fs.promises.rm(destDir, { recursive: true, force: true })
})

test('tar: extractAll refuses hardlink targets that traverse symlinks', async (t) => {
  const archive = await tar.createInMemory()
  await archive.append(
    { path: 'hard.txt', kind: 'hardlink', linkpath: 'linkdir/outside.txt' },
    null
  )
  await archive.finalize()

  const destDir = path.join(
    os.tmpdir(),
    `oro-tar-hardlink-symlink-${Math.random().toString(16).slice(2)}`
  )
  const outsideDir = path.join(
    os.tmpdir(),
    `oro-tar-hardlink-outside-${Math.random().toString(16).slice(2)}`
  )

  await fs.promises.mkdir(destDir, { recursive: true })
  await fs.promises.mkdir(outsideDir, { recursive: true })

  const outsideFile = path.join(outsideDir, 'outside.txt')
  await fs.promises.writeFile(outsideFile, 'pwn')

  try {
    await fs.promises.symlink(outsideDir, path.join(destDir, 'linkdir'))
  } catch (err) {
    t.skip(err.message || 'symlinks unavailable on this platform')
    await archive.close()
    await fs.promises.rm(destDir, { recursive: true, force: true })
    await fs.promises.rm(outsideDir, { recursive: true, force: true })
    return
  }

  await t.rejects(
    archive.extractAll(destDir, { preserveLinks: true }),
    /ELOOP|symlink/i,
    'extractAll rejects hardlink target traversal via symlink'
  )

  await archive.close()
  await fs.promises.rm(destDir, { recursive: true, force: true })
  await fs.promises.rm(outsideDir, { recursive: true, force: true })
})

test('tar: extract refuses hardlink targets that traverse symlinks', async (t) => {
  const archive = await tar.createInMemory()
  await archive.append(
    { path: 'hard.txt', kind: 'hardlink', linkpath: 'linkdir/outside.txt' },
    null
  )
  await archive.finalize()

  const destDir = path.join(
    os.tmpdir(),
    `oro-tar-extract-hardlink-symlink-${Math.random().toString(16).slice(2)}`
  )
  const outsideDir = path.join(
    os.tmpdir(),
    `oro-tar-extract-hardlink-outside-${Math.random().toString(16).slice(2)}`
  )

  await fs.promises.mkdir(destDir, { recursive: true })
  await fs.promises.mkdir(outsideDir, { recursive: true })

  const outsideFile = path.join(outsideDir, 'outside.txt')
  await fs.promises.writeFile(outsideFile, 'pwn')

  try {
    await fs.promises.symlink(outsideDir, path.join(destDir, 'linkdir'))
  } catch (err) {
    t.skip(err.message || 'symlinks unavailable on this platform')
    await archive.close()
    await fs.promises.rm(destDir, { recursive: true, force: true })
    await fs.promises.rm(outsideDir, { recursive: true, force: true })
    return
  }

  const destPath = path.join(destDir, 'hard.txt')
  await t.rejects(
    archive.extract('hard.txt', destPath),
    /ELOOP|symlink/i,
    'extract rejects hardlink target traversal via symlink'
  )

  await archive.close()
  await fs.promises.rm(destDir, { recursive: true, force: true })
  await fs.promises.rm(outsideDir, { recursive: true, force: true })
})

test('tar: append rejects bodies for non-file entries', async (t) => {
  const archive = await tar.createInMemory()

  await t.rejects(
    archive.append(
      { path: 'dir/', kind: 'directory' },
      Buffer.from('not allowed')
    ),
    /directory.*must not include a body/i,
    'directory entries reject non-empty bodies'
  )

  await t.rejects(
    archive.append({ path: 'dir/', kind: 'directory', size: 1 }, null),
    /header\\.size.*0/i,
    'directory entries enforce size 0'
  )

  async function * body () {
    yield Buffer.from('x')
  }

  await t.rejects(
    archive.append({ path: 'dir/', kind: 'directory', size: 0 }, body()),
    /directory.*AsyncIterable/i,
    'directory entries reject async iterable bodies'
  )

  await archive.close()
})

test('tar: char/block device entries expose devmajor/devminor', async (t) => {
  const archive = await tar.createInMemory()

  await archive.append(
    {
      path: 'dev/null',
      kind: 'char-device',
      devmajor: 1,
      devminor: 3
    },
    null
  )

  await archive.finalize()

  const entries = await archive.entries()
  const entry = entries.find((e) => e.path === 'dev/null')

  t.ok(entry, 'device entry is present')
  t.equal(entry.kind, 'char-device', 'device entry kind is char-device')
  t.equal(entry.devmajor, 1, 'devmajor is preserved')
  t.equal(entry.devminor, 3, 'devminor is preserved')

  const stat = await archive.stat('dev/null')
  t.equal(stat.kind, 'char-device', 'stat() returns device kind')
  t.equal(stat.devmajor, 1, 'stat() returns devmajor')
  t.equal(stat.devminor, 3, 'stat() returns devminor')

  const buf = await archive.read('dev/null')
  t.equal(buf.length, 0, 'device entry has no body')

  await archive.close()
})
