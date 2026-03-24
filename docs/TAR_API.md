# Tar Archive API (`oro:tar`)

The `oro:tar` module exposes a small, robust API for working with tar
archives from JavaScript. It is designed for very large archives, supports
random access, and can be backed either by files on disk (optionally
`mmap`-backed on supported platforms) or in-memory buffers.

At a high level:

- Archives on disk are opened via the native tar service and indexed once.
- Individual entries are read on demand via range reads (streaming decode).
- New archives are written entry-by-entry via chunked body writes (streaming encode).
- In-memory archives are backed by a single buffer and use the same reader/index.

## Importing

```js
import * as tar from 'oro:tar'
// or:
// import { TarArchive, open, create, fromBuffer } from 'oro:tar'
```

## Opening archives

- `tar.open(path, options?)`  
  Opens an existing archive on disk.

- `tar.create(path, options?)`  
  Creates (or truncates) an archive for writing.

- `tar.fromBuffer(buffer)`  
  Opens a read‑only archive backed by a `Buffer`, `Uint8Array`, or `ArrayBuffer`.

All helpers return a `TarArchive` instance.

```js
const archive = await tar.open('./assets.tar', { mmap: true })
const entries = await archive.entries()
const logo = await archive.read('images/logo.png')
```

`TarOpenOptions`:

- `writable?: boolean` – set `true` to open a file for writing (implied by `create`).
- `mmap?: boolean` – hint that the runtime may `mmap` very large archives when
  opening them read‑only on supported platforms.
- `uid?: number` – optional global uid metadata for writers.
- `gid?: number` – optional global gid metadata for writers.
- `uname?: string` – optional global uname metadata for writers.
- `gname?: string` – optional global gname metadata for writers.
- `mtime?: number` – optional global mtime metadata for writers (seconds since UNIX epoch).

## `TarArchive`

`TarArchive` represents an open archive descriptor in the runtime:

- `TarArchive.open(path, options?)`
- `TarArchive.create(path, options?)`
- `TarArchive.fromBuffer(buffer)`
- Properties:
  - `id: string` – stable descriptor id (for IPC only).
  - `path: string` – resolved path for file‑backed archives (empty for buffers).
  - `writable: boolean` – whether the archive was opened for writing.
  - `mmap: boolean` – whether the runtime was asked to use `mmap`.
  - `size: number` – total archive size in bytes (for indexed archives).
  - `entryCount: number` – number of indexed entries.
  - `closed: boolean` – `true` after `close()` succeeds.
  - `finalized: boolean` – `true` after `finalize()` succeeds (writers only).

### Metadata and random access

- `archive.entries(): Promise<TarEntryStat[]>`  
  Returns metadata for all indexed entries in the archive.

- `archive.stat(path: string): Promise<TarEntryStat>`  
  Returns metadata for a single entry, or throws if not found.

`TarEntryStat`:

- `path: string`
- `linkpath?: string` – present for symlink/hardlink entries.
- `devmajor?: number` – present for char/block device entries.
- `devminor?: number` – present for char/block device entries.
- `sparse?: { offset: number, length: number }[]` – present for sparse file entries (data regions only).
- `size: number`
- `mode: number`
- `mtime: number` (seconds since UNIX epoch)
- `uid: number`
- `gid: number`
- `uname?: string`
- `gname?: string`
- `kind: 'file' | 'directory' | 'symlink' | 'hardlink' | 'block-device' | 'char-device' | 'fifo' | 'other'`
- `isFile: boolean`
- `isDirectory: boolean`

The native reader builds an in‑memory index at open time for random access.
For very large archives with many entries, this trades O(entryCount) memory
for O(1) lookups by `path`.

The reader supports PAX (`x`/`g`), GNU longname/longlink (`L`/`K`), and GNU
sparse entries (old GNU `S` format, plus PAX `GNU.sparse.*` variants).

### Reading

- `archive.read(path, options?): Promise<Buffer>`  
  Reads a slice of an entry into a single `Buffer`.

- `archive.readStream(path, options?): AsyncIterableIterator<Buffer>`  
  Provides a streaming view over an entry, yielding chunks as `Buffer`s.

`TarReadOptions`:

- `offset?: number` – starting byte offset (default `0`).
- `length?: number` – number of bytes to read (defaults to `size - offset`).
- `signal?: AbortSignal`
- `timeout?: number` – per‑request timeout in milliseconds.

`TarReadStreamOptions`:

- `highWaterMark?: number` – max chunk size in bytes (default `65536`).
- `start?: number` – starting byte offset (default `0`).
- `end?: number` – inclusive end offset (defaults to `entry.size - 1`).
- `signal?: AbortSignal`
- `timeout?: number`

Example (streaming decode):

```js
const chunks = []
for await (const buf of archive.readStream('dir/streamed.txt', {
  highWaterMark: 64 * 1024
})) {
  chunks.push(buf)
}

const full = Buffer.concat(chunks)
```

Attempting to read a directory entry will throw an error with code `EISDIR`.
Sparse file holes are synthesized as zero bytes when reading or streaming.
When extracting sparse files to disk, the runtime attempts to preserve holes by
writing only the sparse data regions.

### Writing

Writing is only allowed for archives opened with `create` (or `open` with
`{ writable: true }`):

- `archive.append(header, body, options?): Promise<void>`
- `archive.finalize(): Promise<void>`

`TarEntryHeader`:

- `path: string` – entry path within the archive.
- `linkpath?: string` – required when `kind` is `'symlink'` or `'hardlink'`.
- `devmajor?: number` – required when `kind` is `'char-device'` or `'block-device'`.
- `devminor?: number` – required when `kind` is `'char-device'` or `'block-device'`.
- `size?: number` – total body size in bytes; required when `body` is an async iterable.
- `sparse?: { offset: number, length: number }[]` – sparse data regions for sparse file entries (sorted, non-overlapping).
- `sparseSize?: number` – logical size of the sparse file entry (defaults to the end of the last region).
- `mode?: number` – file mode (defaults to `0o644` for files and `0o755` for directories).
- `mtime?: number` – modification time in seconds (defaults to the archive global mtime if set, otherwise current time).
- `uid?: number` – optional uid metadata (overrides archive global uid for this entry).
- `gid?: number` – optional gid metadata (overrides archive global gid for this entry).
- `uname?: string` – optional uname metadata (overrides archive global uname for this entry).
- `gname?: string` – optional gname metadata (overrides archive global gname for this entry).
- `kind?: TarEntryKind` – entry type (defaults to `'file'`).

Bodies can be:

- `Buffer`
- `Uint8Array`
- `ArrayBuffer`
- `AsyncIterable<Buffer | Uint8Array>`

For link entries (`kind: 'symlink' | 'hardlink'`), `linkpath` must be provided
and the entry body must be empty (pass `null` or an empty buffer).

For non-file entries (`kind: 'directory' | 'symlink' | 'hardlink' | 'fifo' | 'char-device' | 'block-device'`),
the entry body must be empty and `header.size` (if provided) must be `0`.

For device entries (`kind: 'char-device' | 'block-device'`), `devmajor` and
`devminor` must be provided.

When using an async iterable, `header.size` **must** be provided and match the
total number of bytes yielded; otherwise the append is rejected. For non‑stream
bodies (`Buffer`/`Uint8Array`/`ArrayBuffer`), if `header.size` is provided it
must match the body length; mismatches are rejected rather than padded or
truncated implicitly.

Sparse file entries can be written by providing `header.sparse`. The entry body
must contain only the stored data regions (concatenated in order), and
`header.size`/body length must equal the sum of region lengths. The resulting
entry reports `size === sparseSize` when read back.

All sizes and offsets are JavaScript numbers and must be safe integers
(<= `2^53 - 1`). Sparse maps are capped to 16384 regions to keep PAX metadata
within supported limits.

`TarWriteOptions`:

- `signal?: AbortSignal`
- `timeout?: number`

Example (single buffer):

```js
await archive.append(
  { path: 'foo.txt', mode: 0o644 },
  Buffer.from('hello world')
)
```

Example (streaming encode):

```js
async function* body() {
  yield Buffer.from('chunk-1-')
  yield Buffer.from('chunk-2')
}

await archive.append(
  { path: 'dir/streamed.txt', size: 15 },
  body()
)

await archive.finalize()
```

`finalize()` writes the terminating tar blocks and flushes the underlying
sink. After finalization, the archive descriptor remains open, but additional
writes should not be attempted.

### Extracting entries

`TarArchive` exposes convenience helpers for writing entry contents to disk
using a streaming pipeline:

- `archive.extract(path, destPath, options?): Promise<void>`

This method:

- Resolves `path` within the archive.
- For file entries, streams contents via `TarArchive.readStream` and writes to
  `destPath` using `fs.createWriteStream`, handling backpressure and respecting
  `signal`/`timeout` when provided.
- For directory entries, creates `destPath` as a directory.
- For symlink entries, creates a symlink at `destPath` pointing to the entry's `linkpath`.
- For hardlink entries, creates a hardlink at `destPath` pointing to the entry's `linkpath`
  when the target is available at the computed destination root.
- Other special entries (`'fifo'`, `'char-device'`, `'block-device'`) are not
  materialized by this helper.
- Attempts to preserve `mode` and `mtime` metadata when supported by the
  underlying platform/filesystem. When requested, it will also attempt to
  preserve owner metadata and special mode bits.

Example:

```js
const archive = await tar.open('./assets.tar')
await archive.extract('images/logo.png', './out/logo.png')
await archive.close()
```

To extract all file and directory entries into a directory:

```js
const archive = await tar.open('./assets.tar')
await archive.extractAll('./out/assets')
await archive.close()
```

`extractAll` accepts an optional `filter(entry)` callback which can be used to
select which entries to materialize. Directory entries are created explicitly
when present in the archive so empty directories can be preserved.

By default `extractAll` only materializes file and directory entries. To also
extract symlink and hardlink entries, pass `{ preserveLinks: true }`.
Other special entries (`'fifo'`, `'char-device'`, `'block-device'`) are skipped.

`extractAll` and `extract` refuse to traverse existing symlinks within the
destination directory tree when creating output paths.

`extractAll` and `extract` options:

- `preserveOwner?: boolean` – attempt to apply uid/gid via `chown`/`lchown` (may require privileges; may be ignored by platform).
- `preserveSpecialModes?: boolean` – when `true`, preserves mode bits beyond `0o777` (setuid/setgid/sticky) when supported.

### In-memory archives

`TarArchive.fromBuffer` and the top‑level `tar.fromBuffer` helper open a
read‑only archive backed by an in‑memory buffer:

```js
const raw = await fs.promises.readFile('./assets.tar')
const archive = await tar.fromBuffer(raw)
const logo = await archive.read('images/logo.png')
await archive.close()
```

Buffers are not copied when already a `Buffer`. `Uint8Array` and
`ArrayBuffer` inputs are coerced to a `Buffer` once up front; the native
service then holds a shared reference for the lifetime of the archive
descriptor.

This path is useful for:

- Update artifacts downloaded into memory (see `APPLICATION_UPDATE_PROTOCOL.md`).
- Embedding asset bundles in higher‑level protocols without touching disk.

Writable in-memory archives can be created via `tar.createInMemory(options)`.
These archives behave like any other writable `TarArchive` instance (they
support `append`, `finalize`, `entries`, `stat`, `read`, etc.) but their
contents are kept in memory and can be retrieved as a `Buffer` using
`archive.toBuffer()`:

```js
const archive = await tar.createInMemory({ uid: 1000, uname: 'alice' })
await archive.append({ path: 'mem.txt' }, Buffer.from('in-memory tar payload'))
const buf = await archive.toBuffer()

// buf now contains a complete tar archive; it can be persisted or
// reopened via tar.fromBuffer(buf)
```

## Large archives and `mmap`

For file‑backed archives:

- The native reader uses 64‑bit offsets internally and can index very large
  archives (subject to platform limits and available memory).
- When `mmap: true` is passed and supported, the runtime may map the entire
  archive into memory and satisfy reads via simple `memcpy`, avoiding
  repeated `pread` calls.
- Entry headers and payloads are validated so that header + body + padding
  never exceed the underlying archive size; malformed archives are rejected.
- Very large entry sizes that do not fit in the standard octal field are
  encoded and decoded using the base‑256 extension supported by many tar
  implementations.

Random access is implemented via a per‑archive index:

- `TarArchive.open`/`fromBuffer` build an index of `path -> entry` mappings.
- Reads (`read`/`readStream`) use the index to locate the entry and issue
  a single range read for each requested slice.

For extremely large archives with many entries, this index will dominate
memory usage. In those cases, prefer:

- Streaming decode via `readStream` for individual entries.
- Keeping a single long‑lived `TarArchive` open instead of repeatedly opening
  and closing the same file.

## Conduit and streaming

The tar service uses standard IPC routes:

- `tar.open`, `tar.openBuffer`, `tar.createBuffer`, `tar.close`
- `tar.list`, `tar.stat`, `tar.read`
- `tar.write.begin`, `tar.write.data`, `tar.finalize`
- `tar.buffer`

The `oro:tar` module wraps these routes and uses the shared IPC helpers
(`ipc.request`, `ipc.write`) so it works seamlessly with Conduit‑backed
transports. Callers should use the high‑level `TarArchive` API rather than
invoking the routes directly unless they are implementing lower‑level tools.
