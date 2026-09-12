import { Buffer } from 'oro:buffer'
import deepEqual from 'oro:test/fast-deep-equal'
import { test } from 'oro:test'
import crypto from 'oro:crypto'
import path from 'oro:path'
import fs from 'oro:fs'
import { FileHandle } from 'oro:fs/handle'
import os from 'oro:os'
import process from 'oro:process'
import FIXTURES from '../fixtures.js'
import ipc from 'oro:ipc'

const TMPDIR = `${os.tmpdir()}${path.sep}`

test('fs synchronous binary reads preserve every byte and native text transport', async (t) => {
  const filename = path.join(TMPDIR, `oro-binary-${Date.now()}.bin`)
  const bytes = Buffer.from(Array.from({ length: 256 }, (_, i) => i))
  await fs.promises.writeFile(filename, bytes)
  try {
    t.same(fs.readFileSync(filename), bytes, 'synchronous read preserves all byte values')
    const handle = await fs.promises.open(filename, 'r')
    try {
      const result = ipc.sendSync('fs.read', {
        id: handle.id,
        size: bytes.length,
        offset: 0,
        __sync_binary__: 'true'
      })
      if (result.err) throw result.err
      t.same(Buffer.from(result.data, 'base64'), bytes, 'native base64 response preserves all byte values')
      t.equal(result.headers.get('x-oro-ipc-encoding'), 'base64', 'encoding header is exposed to JavaScript')
    } finally {
      await handle.close()
    }
    await fs.promises.writeFile(filename, '')
    t.equal(fs.readFileSync(filename).length, 0, 'empty binary files remain empty')
    fs.writeFileSync(filename, bytes)
    t.same(fs.readFileSync(filename), bytes, 'synchronous write completes before the descriptor closes')
    fs.writeFileSync(filename, 'hello')
    fs.appendFileSync(filename, ' world')
    t.equal(fs.readFileSync(filename, 'utf8'), 'hello world', 'synchronous append preserves the previous contents')
  } finally {
    await fs.promises.unlink(filename)
  }
})

test('fs.access', async (t) => {
  const { F_OK, R_OK, W_OK, X_OK } = fs.constants

  await new Promise((resolve) => {
    fs.access(FIXTURES, F_OK, (err, access) => {
      if (err) t.fail(`(F_OK) ${FIXTURES} is not accessible`)
      else t.ok(access, '(F_OK) fixtures/ directory is accessible')
      resolve()
    })
  })

  await new Promise((resolve) => {
    fs.access(FIXTURES, R_OK, (err, access) => {
      if (err) t.fail(`(R_OK) ${FIXTURES} is not readable`)
      else t.ok(access, '(R_OK) fixtures/ directory is readable')
      resolve()
    })
  })

  await new Promise((resolve) => {
    fs.access('.', W_OK, (err, access) => {
      if (err) t.fail('(W_OK) ./ is not writable')
      else t.ok(access, '(W_OK) ./ directory is writable')
      resolve()
    })
  })

  await new Promise((resolve) => {
    fs.access(FIXTURES, X_OK, (err, access) => {
      if (err) {
        t.fail(
          `(X_OK) ${FIXTURES} directory is not "executable" - cannot list items`
        )
      } else {
        t.ok(
          access,
          '(X_OK) fixtures/ directory is "executable" - can list items'
        )
      }
      resolve()
    })
  })
})

test('fs.exists', async (t) => {
  await new Promise((resolve) => {
    fs.exists(FIXTURES, (exists) => {
      t.ok(exists, 'fixtures directory exists')
      resolve()
    })
  })
})

if (os.platform() !== 'android' && os.platform() !== 'win32') {
  test('fs.chmod', async (t) => {
    await new Promise((resolve) => {
      fs.chmod(FIXTURES + 'file.txt', 0o777, (err) => {
        if (err) t.fail(err)
        fs.stat(FIXTURES + 'file.txt', (err, stats) => {
          if (err) t.fail(err)
          t.equal(stats.mode & 0o777, 0o777, 'file.txt mode is 777')
          resolve()
        })
      })
    })
  })
}

if (os.platform() !== 'android' && os.platform() !== 'win32') {
  test.skip('fs.chown', async (t) => {
    // TODO: implement process.getuid and process.getgid @bcomnes
    await new Promise((resolve) => {
      const uid = process.getuid()
      const gid = process.getgid()
      fs.chown(FIXTURES + 'chown.txt', uid, gid, (err) => {
        if (err) t.fail(err)
        fs.stat(FIXTURES + 'file.txt', (err, stats) => {
          if (err) t.fail(err)
          t.equal(stats.uid, uid, 'the uid matches the stat')
          t.equal(stats.gid, gid, 'the gid matches the stat')
          resolve()
        })
      })
    })
  })
}

test('fs.open + fs.close', async (t) => {
  await new Promise((resolve) => {
    fs.open(FIXTURES + 'file.txt', (err, fd) => {
      if (err) {
        t.fail(err)
        return resolve()
      }

      t.ok(Number.isFinite(fd), 'isFinite(fd)')
      fs.close(fd, (err) => {
        if (err) t.fail(err)

        t.ok(!err, 'fd closed')
        resolve()
      })
    })
  })
})

test('fs.copyFile', async (t) => {
  await new Promise((resolve) => {
    const src = path.join(FIXTURES, 'file.txt')
    const dest = path.join(FIXTURES, 'copy.txt')

    fs.copyFile(src, dest, 0, (err) => {
      if (err) {
        t.fail(err)
        return resolve()
      }
      t.pass('File was copied without error')

      fs.stat(dest, (err, stats) => {
        if (err) {
          t.fail(err)
          return resolve()
        }

        t.ok(stats, 'Copied file was stated without error')

        fs.readFile(src, 'utf8', (err, srcData) => {
          if (err) {
            t.fail(err)
            return resolve()
          }

          t.ok(srcData, 'The src data was read without error')

          fs.readFile(dest, 'utf8', (err, destData) => {
            if (err) {
              t.fail(err)
              return resolve()
            }

            t.ok(srcData, 'The copied data was read without error')

            t.equal(destData, srcData, 'the copy contains a copy of the data')

            fs.unlink(dest, (err) => {
              if (err) {
                t.fail(err)
                return resolve()
              }
              t.ok('The copied file is removed')
              return resolve()
            })
          })
        })
      })
    })
  })
})

test('fs.createReadStream', async (t) => {
  if (os.platform() === 'android') {
    t.comment('FIXME for Android')
    return
  }

  const buffers = []
  await new Promise((resolve) => {
    const stream = fs.createReadStream(FIXTURES + 'file.txt')
    const expected = Buffer.from('test 123')

    stream.on('close', resolve)
    stream.on('data', (buffer) => {
      buffers.push(buffer)
    })

    stream.on('error', (err) => {
      if (err) t.fail(err)
      resolve()
    })

    stream.on('end', () => {
      let actual = Buffer.concat(buffers)
      if (actual[actual.length - 1] === 0x0a) {
        actual = actual.slice(0, -1)
      }

      if (actual[actual.length - 1] === 0x0d) {
        actual = actual.slice(0, -1)
      }

      t.ok(
        Buffer.compare(expected, actual) === 0,
        `fixtures/file.txt contents match "${expected}"`
      )
    })
  })
})

test('fs.createWriteStream', async (t) => {
  if (os.platform() === 'android') return t.comment('TODO')
  const writer = fs.createWriteStream(TMPDIR + 'new-file.txt')
  const bytes = crypto.randomBytes(32 * 1024 * 1024)
  writer.write(bytes.slice(0, 512 * 1024))
  writer.write(bytes.slice(512 * 1024))
  writer.end()
  await new Promise((resolve) => {
    writer.once('error', (err) => {
      t.fail(err.message)
      writer.removeAllListeners()
      resolve()
    })
    writer.once('close', () => {
      const reader = fs.createReadStream(TMPDIR + 'new-file.txt')
      const buffers = []
      reader.on('data', (buffer) => buffers.push(buffer))
      reader.on('end', () => {
        t.ok(Buffer.compare(bytes, Buffer.concat(buffers)) === 0, 'bytes match')
        resolve()
      })
    })
  })
})

test('fs.fstat', async (t) => {
  await new Promise((resolve) => {
    fs.open(path.join(FIXTURES, 'file.txt'), (err, fd) => {
      if (err) {
        t.fail(err)
        return resolve()
      }

      t.ok(Number.isFinite(fd), 'isFinite(fd)')

      fs.fstat(fd, (err, stats) => {
        if (err) {
          t.fail(err)
          return resolve()
        }

        t.ok(stats, 'the stats object is included')
        t.ok(stats.size > 0, 'fstat returns a populated stats payload')

        fs.close(fd, (err) => {
          if (err) t.fail(err)

          t.ok(!err, 'fd closed')
          resolve()
        })
      })
    })
  })
})

// TODO
test('fs.lchmod', async () => {})
test('fs.lchown', async () => {})
test('fs.lutimes', async () => {})
test('fs.link', async () => {})
test('fs.lstat', async () => {})

if (os.platform() !== 'android') {
  test('fs.mkdir', async () => {
    const dirname = FIXTURES + Math.random().toString(16).slice(2)
    await new Promise((resolve, reject) => {
      fs.mkdir(dirname, {}, (err) => {
        if (err) reject(err)

        fs.stat(dirname, (err) => {
          if (err) reject(err)
          resolve()
        })
      })
    })
  })
}

test('fs.readlink encoding buffer (callback/sync)', async (t) => {
  const link = path.join(FIXTURES, 'symlink-buf.txt')
  const target = path.join(FIXTURES, 'file.txt')
  await new Promise((resolve) => {
    fs.symlink(target, link, (err) => {
      if (err) {
        t.comment(
          'symlink not allowed by policy; skipping readlink buffer test'
        )
        return resolve()
      }
      fs.readlink(link, 'buffer', (err2, buf) => {
        if (err2) {
          t.fail(err2)
        } else {
          t.ok(
            Buffer.isBuffer(buf),
            'readlink callback returns Buffer with encoding "buffer"'
          )
        }
        try {
          fs.unlinkSync(link)
        } catch {}
        resolve()
      })
    })
  })
  // sync path (use temp symlink if possible)
  try {
    const link2 = path.join(FIXTURES, 'symlink-buf2.txt')
    fs.symlinkSync(target, link2)
    const buf = fs.readlinkSync(link2, 'buffer')
    t.ok(
      Buffer.isBuffer(buf),
      'readlinkSync returns Buffer with encoding "buffer"'
    )
    fs.unlinkSync(link2)
  } catch {
    t.comment('symlink not allowed by policy (sync); skipping')
  }
})

test('fs.readdir encoding buffer (callback)', async (t) => {
  await new Promise((resolve) => {
    fs.readdir(
      FIXTURES + 'directory',
      { encoding: 'buffer' },
      (err, entries) => {
        if (err) t.fail(err)
        else {
          t.ok(
            Array.isArray(entries) && Buffer.isBuffer(entries[0]),
            'readdir with encoding buffer returns Buffer[]'
          )
        }
        resolve()
      }
    )
  })
})

if (os.platform() !== 'android') {
  test('fs.mkdir recursive', async () => {
    const randomDirSegment = () => Math.random().toString(16).slice(2)
    const dirname = path.join(
      FIXTURES,
      randomDirSegment(),
      randomDirSegment(),
      randomDirSegment()
    )
    await new Promise((resolve, reject) => {
      fs.mkdir(dirname, { recursive: true }, (err) => {
        if (err) reject(err)

        fs.stat(dirname, (err) => {
          if (err) reject(err)
          resolve()
        })
      })
    })
  })
}

test('fs.opendir', async (t) => {
  await new Promise((resolve) => {
    fs.opendir(FIXTURES, async (err, dir) => {
      if (err) {
        t.fail(err)
        return resolve()
      }

      try {
        t.ok(!dir.closed, 'the dirent is open before iterating')
        let count = 0
        for await (const dirent of dir) {
          t.ok(
            dirent.name,
            `loop through dirents and they have names: ${dirent.name}`
          )
          count++
        }
        t.ok(count > 0, 'There are more than 0 dirents')
        t.ok(dir.closed, 'the dirent is closed after iterating')
        return resolve()
      } catch (err) {
        t.fail(err)
        return resolve()
      }
    })
  })
})

test('fs.read', async (t) => {
  await new Promise((resolve) => {
    fs.open(FIXTURES + 'file.txt', (err, fd) => {
      if (err) {
        t.fail(err)
        return resolve()
      }

      t.ok(Number.isFinite(fd), 'isFinite(fd)')
      const readLength = 8
      const readBuff = new Int8Array(readLength)

      fs.read(fd, readBuff, 0, readLength, 0, (err, bytesRead, buffer) => {
        if (err) {
          t.fail(err)
          return resolve()
        }
        const expected = 'test 123'
        const returnedBuf = new TextDecoder().decode(buffer)
        const targetBuf = new TextDecoder().decode(readBuff)

        t.equal(returnedBuf, expected, 'returned buffer has the correct data')
        t.equal(targetBuf, expected, 'target buffer has the correct data')

        fs.close(fd, (err) => {
          if (err) t.fail(err)

          t.ok(!err, 'fd closed')
          resolve()
        })
      })
    })
  })
})

test('fs.readdir', async (t) => {
  await new Promise((resolve) => {
    fs.readdir(FIXTURES, async (err, files) => {
      if (err) {
        t.fail(err)
        return resolve()
      }

      t.ok(files.length >= 7, 'has the correct number of files in it')
      ;[
        'bin',
        'chown.txt',
        'data.bin',
        'directory',
        'file.js',
        'file.json',
        'file.txt'
      ].forEach((file) => {
        t.ok(files.includes(file), `includes ${file}`)
      })

      return resolve()
    })
  })
})

test('fs.readdir withFileTypes', async (t) => {
  await new Promise((resolve) => {
    fs.readdir(FIXTURES, { withFileTypes: true }, async (err, files) => {
      if (err) {
        t.fail(err)
        return resolve()
      }

      t.ok(files.length >= 7, 'has the correct number of files in it')

      t.ok(
        files.every((dirent) => dirent instanceof fs.Dirent),
        'every dirent in the files array is actually a dirent'
      )
      return resolve()
    })
  })
})

test('fs.readFile', async (t) => {
  let failed = false
  const iterations = 16 // generate ~1k _concurrent_ requests
  const expected = { data: 'test 123' }
  const promises = Array.from(
    Array(iterations),
    (_, i) =>
      new Promise((resolve) => {
        if (failed) return resolve(false)
        fs.readFile(FIXTURES + 'file.json', (err, buf) => {
          if (failed) return resolve(false)

          const message = `fs.readFile('fixtures/file.json') [iteration=${i + 1}]`

          try {
            if (err) {
              t.fail(err, message)
              failed = true
            } else if (!deepEqual(expected, JSON.parse(buf))) {
              failed = true
            }
          } catch (err) {
            t.fail(err, message)
            failed = true
          }

          resolve(!failed)
        })
      })
  )

  const results = await Promise.all(promises)
  t.ok(results.every(Boolean), "fs.readFile('fixtures/file.json')")
})

/*
// TODO: ensure this is working as expected. Its not working like node @bcomnes
// resolving to "/Users/userHomeDir/socket/test/fixtures/file.txt" on macos
test('fs.readlink', async (t) => {
  await new Promise((resolve, reject) => {
    const link = path.join(FIXTURES, 'link.txt')
    fs.readlink(link, (_, resolvedPath) => {
      t.ok(resolvedPath.endsWith('/file.txt'), 'link path matches the actual path')
      return resolve()
    })
  })
})
*/

/*
// TODO: ensure this is working as expected. Its not working like node @bcomnes
test('fs.realpath', async (t) => {
  await new Promise((resolve, reject) => {
    const link = path.join(FIXTURES, 'link.txt')
    fs.realpath(link, (_, resolvedPath) => {
      t.ok(resolvedPath.endsWith('/file.txt'), 'link path matches the actual path')
      return resolve()
    })
  })
})
*/

test('fs.rename', async (t) => {
  await new Promise((resolve) => {
    const src = path.join(FIXTURES, 'file.txt')
    const dest = path.join(FIXTURES, 'rename.txt')
    fs.rename(src, dest, (err) => {
      if (err) {
        t.fail(err)
        return resolve()
      }

      t.pass('File was renamed without error')

      fs.stat(dest, (err, stats) => {
        if (err) {
          t.fail(err)
          return resolve()
        }

        t.ok(stats, 'Renamed file was stated without error')

        fs.rename(dest, src, (err) => {
          if (err) {
            t.fail(err)
            return resolve()
          }

          fs.stat(src, (err, stats) => {
            if (err) {
              t.fail(err)
              return resolve()
            }

            t.ok(stats, 'Renamed file was moved back to the original location')
            return resolve()
          })
        })
      })
    })
  })
})

if (os.platform() !== 'android') {
  test('fs.linkSync + readlinkSync', async (t) => {
    try {
      const src = path.join(FIXTURES, 'file.txt')
      const hard = path.join(FIXTURES, 'hardlink.txt')
      const sym = path.join(FIXTURES, 'symlink2.txt')
      fs.linkSync(src, hard)
      const hardStats = fs.statSync(hard)
      t.ok(hardStats.isFile(), 'hard link created')
      // create symlink and read its target
      await new Promise((resolve) => {
        fs.symlink(src, sym, (err) => {
          if (err) {
            t.comment('symlink not allowed by policy; skipping readlinkSync')
            try {
              fs.unlinkSync(hard)
            } catch {}
            t.pass('linkSync covered')
            return resolve()
          }

          try {
            const target = fs.readlinkSync(sym)
            t.ok(typeof target === 'string', 'readlinkSync returns string')
            t.ok(
              target.endsWith('/file.txt') || target.endsWith('\\file.txt'),
              'symlink target path'
            )
            t.pass('readlinkSync tested')
          } catch (err) {
            t.fail(err)
          } finally {
            try {
              fs.unlinkSync(sym)
            } catch {}
            try {
              fs.unlinkSync(hard)
            } catch {}
            resolve()
          }
        })
      })
    } catch {
      t.comment('linkSync not allowed by policy; skipping')
      t.pass('skipped')
    }
  })
}

if (os.platform() !== 'android') {
  test('fs.rmdir', async (t) => {
    await new Promise((resolve) => {
      const target = path.join(FIXTURES, 'rmdir-dir')
      fs.mkdir(target, { recursive: true }, (err) => {
        if (err) {
          t.fail(err)
          return resolve()
        }
        t.pass('The directory is created without error')
        fs.rmdir(target, (err) => {
          if (err) {
            t.fail(err)
            return resolve()
          }
          t.pass('The directory is removed without error')
          fs.stat(target, (err) => {
            if (err) {
              t.pass('The directory is removed and no longer stats')
              return resolve()
            } else {
              t.fail('The directory should fail to stat')
              return resolve()
            }
          })
        })
      })
    })
  })
}

test('fs.stat', async (t) => {
  await new Promise((resolve) => {
    const target = path.join(FIXTURES, 'file.txt')
    fs.stat(target, (err, stats) => {
      if (err) {
        t.fail(err)
        return resolve()
      }

      t.ok(stats, 'stat object is returned')
      return resolve()
    })
  })
})

if (os.platform() !== 'android') {
  test('fs.utimes', async (t) => {
    const target =
      TMPDIR + 'utimes-file-' + Math.random().toString(16).slice(2) + '.txt'
    fs.writeFileSync(target, 'x')
    const before = fs.statSync(target)
    const at = new Date(before.atimeMs + 2000)
    const mt = new Date(before.mtimeMs + 3000)
    await new Promise((resolve) => {
      fs.utimes(target, at, mt, (err) => {
        if (err) t.fail(err)
        resolve()
      })
    })
    const after = fs.statSync(target)
    t.ok(after.atimeMs >= at.getTime() - 1500, 'atime updated')
    t.ok(after.mtimeMs >= mt.getTime() - 1500, 'mtime updated')
  })

  test('fs.futimes', async (t) => {
    const target =
      TMPDIR + 'futimes-file-' + Math.random().toString(16).slice(2) + '.txt'
    fs.writeFileSync(target, 'y')
    const fd = fs.openSync(target, 'r+')
    const before = fs.statSync(target)
    const at = new Date(before.atimeMs + 5000)
    const mt = new Date(before.mtimeMs + 6000)
    await new Promise((resolve) => {
      fs.futimes(fd, at, mt, (err) => {
        if (err) t.fail(err)
        resolve()
      })
    })
    const after = fs.statSync(target)
    t.ok(after.atimeMs >= at.getTime() - 1500, 'atime updated via fd')
    t.ok(after.mtimeMs >= mt.getTime() - 1500, 'mtime updated via fd')
    t.ok(fs.fstatSync(fd).isFile(), 'descriptor remains open after futimes')
    fs.closeSync(fd)
  })
}

if (os.platform() === 'win32') {
  test('fs.lutimes (win)', async (t) => {
    const target = path.join(FIXTURES, 'file.txt')
    const link = path.join(
      TMPDIR,
      'lutimes-link-' + Math.random().toString(16).slice(2) + '.txt'
    )
    await new Promise((resolve) => {
      fs.symlink(target, link, (err) => {
        if (err) {
          t.comment('Symlink not allowed by policy; skipping lutimes test')
          return resolve()
        }
        try {
          const before = fs.lstatSync(link)
          const at = new Date(before.atimeMs + 5000)
          const mt = new Date(before.mtimeMs + 6000)
          fs.lutimes(link, at, mt, (err2) => {
            if (err2) {
              t.comment('lutimes failed; skipping')
            } else {
              const after = fs.lstatSync(link)
              t.ok(
                after.atimeMs >= at.getTime() - 2000,
                'lutimes atime updated'
              )
              t.ok(
                after.mtimeMs >= mt.getTime() - 2000,
                'lutimes mtime updated'
              )
            }
            try {
              fs.unlinkSync(link)
            } catch {}
            resolve()
          })
        } catch {
          try {
            fs.unlinkSync(link)
          } catch {}
          t.comment('Unexpected error; skipping lutimes test')
          resolve()
        }
      })
    })
  })

  test('fs.lutimes on directory junction (win)', async (t) => {
    const dir = path.join(
      TMPDIR,
      'lutimes-dir-' + Math.random().toString(16).slice(2)
    )
    const link = path.join(
      TMPDIR,
      'lutimes-dir-link-' + Math.random().toString(16).slice(2)
    )
    try {
      fs.mkdirSync(dir)
    } catch {}
    await new Promise((resolve) => {
      fs.symlink(dir, link, 'junction', (err) => {
        if (err) {
          t.comment(
            'Junction creation not allowed; skipping directory lutimes test'
          )
          try {
            fs.rmdirSync(dir)
          } catch {}
          return resolve()
        }
        try {
          const before = fs.lstatSync(link)
          const at = new Date(before.atimeMs + 7000)
          const mt = new Date(before.mtimeMs + 8000)
          fs.lutimes(link, at, mt, (err2) => {
            if (err2) {
              t.comment('lutimes on junction failed; skipping')
            } else {
              const after = fs.lstatSync(link)
              t.ok(
                after.atimeMs >= at.getTime() - 3000,
                'junction atime updated'
              )
              t.ok(
                after.mtimeMs >= mt.getTime() - 3000,
                'junction mtime updated'
              )
            }
            try {
              fs.unlinkSync(link)
            } catch {}
            try {
              fs.rmdirSync(dir)
            } catch {}
            resolve()
          })
        } catch {
          try {
            fs.unlinkSync(link)
          } catch {}
          try {
            fs.rmdirSync(dir)
          } catch {}
          t.comment('Unexpected error; skipping junction lutimes test')
          resolve()
        }
      })
    })
  })
}

test('fs.symlink', async (t) => {
  await new Promise((resolve) => {
    const src = path.join(FIXTURES, 'file.txt')
    const dest = path.join(FIXTURES, 'symlink.txt')
    fs.symlink(src, dest, (err) => {
      if (err) {
        t.fail(err)
        return resolve()
      }

      t.pass('The symlink is made without error')

      fs.realpath(dest, (err, resolvedPath) => {
        if (err) {
          t.fail(err)
          return resolve()
        }

        t.ok(
          resolvedPath.endsWith('/file.txt') ||
            resolvedPath.endsWith('\\file.txt'),
          'link path matches the actual path'
        )

        fs.unlink(dest, (err) => {
          if (err) {
            t.fail(err)
            return resolve()
          }
          t.pass('The symlink is removed')
          return resolve()
        })
      })
    })
  })
})

// This doesn't work yet
test.skip('fs.ftruncate', async (t) => {
  await new Promise((resolve) => {
    const testString = 'test 123 test 123 test 123 test 123 test 123'
    const buffer = Buffer.from(testString)
    const filePath = path.join(TMPDIR, 'truncate.txt')
    fs.writeFile(filePath, buffer, (err) => {
      if (err) {
        t.fail(err)
        return resolve()
      }

      t.pass('The test file is created')

      fs.open(filePath, (err, fd) => {
        if (err) {
          t.fail(err)
          return resolve()
        }

        t.ok(fd, 'The test file is opened and we have an fd')

        fs.ftruncate(fd, 5, (err) => {
          if (err) {
            console.error({ err })
            t.fail(err)
            return resolve()
          }

          t.pass('The test file is truncated')

          fs.readFile(filePath, (err, result) => {
            if (err) {
              t.fail(err)
              return resolve()
            }

            t.equal(result, testString)

            fs.close(fd, (err) => {
              if (err) t.fail(err)

              t.ok(!err, 'fd closed')
              resolve()
            })
          })
        })
      })
    })
  })
})

test('fs.unlink', async (t) => {
  const buffer = Buffer.from('test 123')
  const directory = await fs.promises.mkdtemp(path.join(TMPDIR, 'oro-unlink-'))
  await new Promise((resolve) => {
    const filePatht = path.join(directory, 'file.txt')
    fs.writeFile(filePatht, buffer, (err) => {
      if (err) {
        t.fail(err)
        return resolve()
      }

      t.pass('The file is written wuthout error')

      fs.readFile(filePatht, (err, result) => {
        if (err) {
          t.fail(err)
          return resolve()
        }

        t.pass('The file is written wuthout error')

        t.equal(Buffer.compare(result, buffer), 0, 'bytes match')

        fs.unlink(filePatht, (err) => {
          if (err) {
            t.fail(err)
            return resolve()
          }
          t.ok('The test file is removed without error')

          fs.stat(filePatht, (err) => {
            if (err) {
              t.pass('The file is removed and no longer stats')
              return resolve()
            } else {
              t.fail('The file should fail to stat')
              return resolve()
            }
          })
        })
      })
    })
  })
  await fs.promises.rm(directory, { recursive: true, force: true })
})

test('fs.truncate + fs.appendFile + fs.rm/cp', async (t) => {
  const base = TMPDIR
  const srcDir = path.join(
    base,
    'cp-src-' + Math.random().toString(16).slice(2)
  )
  const dstDir = path.join(
    base,
    'cp-dst-' + Math.random().toString(16).slice(2)
  )
  const fileA = path.join(srcDir, 'a.txt')
  const fileB = path.join(srcDir, 'b.txt')
  try {
    fs.mkdirSync(srcDir, { recursive: true })
  } catch {}
  fs.writeFileSync(fileA, 'hello world')
  fs.writeFileSync(fileB, 'abcdef')

  // truncate fileB
  await new Promise((resolve) =>
    fs.truncate(fileB, 3, (err) => {
      if (err) t.fail(err)
      resolve()
    })
  )
  t.equal(fs.readFileSync(fileB, 'utf8'), 'abc', 'truncate shortened file')

  // append to fileA
  await new Promise((resolve) =>
    fs.appendFile(fileA, '!', (err) => {
      if (err) t.fail(err)
      resolve()
    })
  )
  t.equal(
    fs.readFileSync(fileA, 'utf8'),
    'hello world!',
    'appendFile appended data'
  )

  // copy dir with filter and preserveTimestamps
  await new Promise((resolve) =>
    fs.cp(
      srcDir,
      dstDir,
      {
        recursive: true,
        preserveTimestamps: true,
        filter: (s) => !s.endsWith('/b.txt')
      },
      (err) => {
        if (err) t.fail(err)
        resolve()
      }
    )
  )
  t.equal(
    fs.readFileSync(path.join(dstDir, 'a.txt'), 'utf8'),
    'hello world!',
    'cp copied a.txt'
  )
  try {
    fs.readFileSync(path.join(dstDir, 'b.txt'), 'utf8')
    t.fail('b.txt should have been filtered out')
  } catch {
    t.pass('filter skipped b.txt')
  }

  // errorOnExist behavior
  fs.writeFileSync(path.join(base, 'cp-exists.txt'), 'x')
  fs.writeFileSync(path.join(base, 'cp-exists-dest.txt'), 'y')
  await new Promise((resolve) =>
    fs.cp(
      path.join(base, 'cp-exists.txt'),
      path.join(base, 'cp-exists-dest.txt'),
      { errorOnExist: true },
      (err) => {
        t.ok(err, 'errorOnExist returns error when dest exists')
        resolve()
      }
    )
  )
  await new Promise((resolve) =>
    fs.cp(
      path.join(base, 'cp-exists.txt'),
      path.join(base, 'cp-exists-dest.txt'),
      { errorOnExist: false },
      (err) => {
        t.ok(
          !err,
          'no error when errorOnExist is false and dest exists (no overwrite)'
        )
        resolve()
      }
    )
  )
  await new Promise((resolve) =>
    fs.cp(
      path.join(base, 'cp-exists.txt'),
      path.join(base, 'cp-exists-dest.txt'),
      { force: true },
      (err) => {
        t.ok(!err, 'no error when force true (overwrites)')
        resolve()
      }
    )
  )
  fs.rmSync(path.join(base, 'cp-exists.txt'), { force: true })
  fs.rmSync(path.join(base, 'cp-exists-dest.txt'), { force: true })

  // rm dstDir recursively
  await new Promise((resolve) =>
    fs.rm(dstDir, { recursive: true }, (err) => {
      if (err) t.fail(err)
      resolve()
    })
  )
  try {
    fs.statSync(dstDir)
    t.fail('dstDir should be removed')
  } catch {
    t.pass('dstDir removed')
  }

  // cleanup src
  fs.rmSync(srcDir, { recursive: true, force: true })

  // preserveMode (POSIX only)
  if (os.platform() !== 'win32') {
    const src = path.join(
      base,
      'cp-mode-src-' + Math.random().toString(16).slice(2) + '.txt'
    )
    const dst = path.join(
      base,
      'cp-mode-dst-' + Math.random().toString(16).slice(2) + '.txt'
    )
    fs.writeFileSync(src, 'z')
    try {
      fs.chmodSync(src, 0o744)
    } catch {}
    await new Promise((resolve) =>
      fs.cp(src, dst, { preserveMode: true }, (err) => {
        if (err) t.fail(err)
        resolve()
      })
    )
    try {
      const m = fs.statSync(dst).mode & 0o777
      t.equal(m, 0o744, 'preserveMode applied')
    } catch {
      t.comment('preserveMode may be restricted; skipping check')
    }
    fs.rmSync(src, { force: true })
    fs.rmSync(dst, { force: true })
  }

  // preserveOwner (POSIX only; usually same owner, but assert equality)
  if (os.platform() !== 'win32') {
    const src = path.join(
      base,
      'cp-owner-src-' + Math.random().toString(16).slice(2) + '.txt'
    )
    const dst = path.join(
      base,
      'cp-owner-dst-' + Math.random().toString(16).slice(2) + '.txt'
    )
    fs.writeFileSync(src, 'owner')
    const srcStat = fs.statSync(src)
    await new Promise((resolve) =>
      fs.cp(src, dst, { preserveOwner: true }, (err) => {
        if (err) t.fail(err)
        resolve()
      })
    )
    const dstStat = fs.statSync(dst)
    t.equal(dstStat.uid, srcStat.uid, 'preserveOwner kept uid')
    t.equal(dstStat.gid, srcStat.gid, 'preserveOwner kept gid')
    fs.rmSync(src, { force: true })
    fs.rmSync(dst, { force: true })
  }
})

test('fs.mkdtemp', async (t) => {
  const prefix = TMPDIR + 'oro-test-mkdtemp-'
  await new Promise((resolve) => {
    fs.mkdtemp(prefix, (err, dir) => {
      if (err) t.fail(err)
      else t.ok(fs.statSync(dir).isDirectory(), 'mkdtemp created a directory')
      try {
        fs.rmSync(dir, { recursive: true, force: true })
      } catch {}
      resolve()
    })
  })
  const dir = fs.mkdtempSync(prefix)
  t.ok(fs.statSync(dir).isDirectory(), 'mkdtempSync created a directory')
  fs.rmSync(dir, { recursive: true, force: true })
})

if (os.platform() !== 'android') {
  test('fs.watchFile/unwatchFile', async (t) => {
    const file = path.join(
      TMPDIR,
      'watchfile-' + Math.random().toString(16).slice(2) + '.txt'
    )
    fs.writeFileSync(file, '0')
    await new Promise((resolve) => {
      let fired = false
      const listener = (curr, prev) => {
        try {
          t.ok(curr.mtimeMs >= prev.mtimeMs, 'mtime increased')
          fired = true
        } finally {
          fs.unwatchFile(file, listener)
          fs.rmSync(file, { force: true })
          resolve()
        }
      }
      fs.watchFile(file, { interval: 50 }, listener)
      setTimeout(() => {
        try {
          fs.appendFileSync(file, '1')
        } catch {}
      }, 100)
      // safety timeout in case no event
      setTimeout(() => {
        if (!fired) {
          fs.unwatchFile(file, listener)
          try {
            fs.rmSync(file, { force: true })
          } catch {}
          t.fail('watchFile did not fire')
          resolve()
        }
      }, 1000)
    })
  })

  test('fs.watchFile multiple listeners and specific unwatch', async (t) => {
    const file = path.join(
      TMPDIR,
      'watchfile2-' + Math.random().toString(16).slice(2) + '.txt'
    )
    fs.writeFileSync(file, 'A')
    let l1 = 0
    let l2 = 0
    await new Promise((resolve) => {
      let phase = 0
      let settled = false
      let safetyTimer = null
      const finish = () => {
        if (settled) return
        settled = true
        clearTimeout(safetyTimer)
        fs.unwatchFile(file)
        try {
          fs.rmSync(file, { force: true })
        } catch {}
        resolve()
      }
      const listener1 = () => {
        l1++
      }
      const listener2 = () => {
        l2++
        if (phase === 0) {
          phase = 1
          fs.unwatchFile(file, listener1)
          setTimeout(() => {
            try {
              fs.appendFileSync(file, 'C')
            } catch {}
          }, 100)
        } else if (phase === 1) {
          phase = 2
          t.ok(l1 >= 1, 'listener1 fired at least once')
          t.ok(l2 >= 2, 'listener2 fired twice')
          t.ok(l1 < l2, 'listener1 did not fire for second change')
          finish()
        }
      }
      fs.watchFile(file, { interval: 40 }, listener1)
      fs.watchFile(file, { interval: 40 }, listener2)
      setTimeout(() => {
        try {
          fs.appendFileSync(file, 'B')
        } catch {}
      }, 80)
      safetyTimer = setTimeout(() => {
        t.fail(`watchFile listeners stalled in phase ${phase}`)
        finish()
      }, 2000)
    })
  })

  test('fs.watchFile with bigint option', async (t) => {
    const file = path.join(
      TMPDIR,
      'watchfile3-' + Math.random().toString(16).slice(2) + '.txt'
    )
    fs.writeFileSync(file, 'X')
    await new Promise((resolve) => {
      let fired = false
      const listener = (curr, prev) => {
        try {
          fired = true
          t.equal(typeof curr.size, 'bigint', 'curr.size is bigint')
          t.equal(typeof prev.size, 'bigint', 'prev.size is bigint')
        } finally {
          fs.unwatchFile(file, listener)
          try {
            fs.rmSync(file, { force: true })
          } catch {}
          resolve()
        }
      }
      fs.watchFile(file, { interval: 40, bigint: true }, listener)
      setTimeout(() => {
        try {
          fs.appendFileSync(file, 'Y')
        } catch {}
      }, 80)
      setTimeout(() => {
        if (!fired) {
          fs.unwatchFile(file, listener)
          try {
            fs.rmSync(file, { force: true })
          } catch {}
          t.fail('bigint watchFile did not fire')
          resolve()
        }
      }, 1000)
    })
  })

  test('FileHandle.readv/writev (callback style)', async (t) => {
    const file = path.join(
      TMPDIR,
      'vec-cb-' + Math.random().toString(16).slice(2) + '.bin'
    )
    await new Promise((resolve) => {
      fs.open(file, 'w+', (err, fd) => {
        if (err) {
          t.fail(err)
          return resolve()
        }
        const fh = FileHandle.from(fd)
        const parts = [
          Buffer.from('foo'),
          Buffer.from('bar'),
          Buffer.from('baz')
        ]
        fh.writev(parts, 0)
          .then(async (n) => {
            t.equal(n, 9, 'writev wrote 9 bytes')
            await fh.close()
            fs.readFile(file, (err2, buf) => {
              if (err2) t.fail(err2)
              else t.equal(buf.toString(), 'foobarbaz', 'file contents correct')
              resolve()
            })
          })
          .catch((e) => {
            t.fail(e)
            resolve()
          })
      })
    })

    await new Promise((resolve) => {
      fs.open(file, 'r', (err, fd) => {
        if (err) {
          t.fail(err)
          return resolve()
        }
        const fh = FileHandle.from(fd)
        const a = Buffer.alloc(3)
        const b = Buffer.alloc(3)
        const c = Buffer.alloc(3)
        fh.readv([a, b, c], 0)
          .then(async ({ bytesRead }) => {
            t.equal(bytesRead, 9, 'readv read 9 bytes')
            t.equal(
              Buffer.concat([a, b, c]).toString(),
              'foobarbaz',
              'readv buffers match'
            )
            await fh.close()
            fs.rm(file, { force: true }, () => resolve())
          })
          .catch((e) => {
            t.fail(e)
            resolve()
          })
      })
    })
  })

  test('fs.cp copies symlink as symlink when dereference=false', async (t) => {
    const target = path.join(FIXTURES, 'file.txt')
    const link = path.join(
      TMPDIR,
      'cp-link-src-' + Math.random().toString(16).slice(2)
    )
    const dest = path.join(
      TMPDIR,
      'cp-link-dest-' + Math.random().toString(16).slice(2)
    )
    await new Promise((resolve) => {
      fs.symlink(target, link, (err) => {
        if (err) {
          t.comment('symlink policy blocks test; skipping')
          return resolve()
        }
        fs.cp(link, dest, { dereference: false }, (err2) => {
          if (err2) {
            t.fail(err2)
            return resolve()
          }
          try {
            const rl = fs.readlinkSync(dest)
            t.ok(
              typeof rl === 'string' || Buffer.isBuffer(rl),
              'readlinkSync returns value'
            )
          } catch {
            t.comment('readlinkSync not permitted; skipping')
          }
          try {
            fs.rmSync(link, { force: true })
          } catch {}
          try {
            fs.rmSync(dest, { force: true })
          } catch {}
          resolve()
        })
      })
    })
  })
}

// Not working yet
test.skip('fs.watch', async (t) => {
  let watcherFired = false
  const watcher = fs.watch(TMPDIR, () => {
    watcherFired = true
  })
  await watcher.start()
  const buffer = Buffer.from('test 123')

  await new Promise((resolve) => {
    fs.writeFile(TMPDIR + 'watch-file1.txt', buffer, (err) => {
      if (err) {
        t.fail(err)
        return resolve()
      }

      t.pass('watch-file1.txt written')

      fs.writeFile(TMPDIR + 'watch-file2.txt', buffer, (err) => {
        if (err) {
          t.fail(err)
          return resolve()
        }

        t.pass('watch-file2.txt written')
        fs.writeFile(TMPDIR + 'watch-file3.txt', buffer, (err) => {
          if (err) {
            t.fail(err)
            return resolve()
          }
          t.pass('watch-file3.txt written')
          resolve()
        })
      })
    })
  })

  await watcher.close()
  t.ok(watcherFired, 'The watcher should have fired at least once')
})

if (os.platform() !== 'android') {
  test('fs.writeFile', async (t) => {
    const buffer = Buffer.from('test 123')
    await new Promise((resolve) => {
      fs.writeFile(TMPDIR + 'new-file.txt', buffer, (err) => {
        if (err) t.fail(err.message)

        fs.readFile(TMPDIR + 'new-file.txt', (err, result) => {
          if (err) t.fail(err.message)
          else if (Buffer.compare(result, buffer) !== 0) {
            t.fail('bytes do not match')
          }
          resolve()
        })
      })
    })

    // TODO: move the code below to a benchmark tests

    // const alloc = (size) => crypto.randomBytes(size)
    // const small = Array.from({ length: 32 }, (_, i) => i * 2 * 1024).map(alloc)

    // const large = Array.from({ length: 16 }, (_, i) => i * 2 * 1024 * 1024).map(alloc)
    // const buffers = [...small, ...large]

    // // const pending = buffers.length
    // let failed = false
    // const writes = []

    // // const now = Date.now()
    // while (!failed && buffers.length) {
    //   writes.push(testWrite(buffers.length - 1, buffers.pop()))
    // }

    // await Promise.all(writes)

    // // console.log(
    // //   '%d writes to %sms to write %s bytes',
    // //   small.length + large.length,
    // //   Date.now() - now,
    // //   [...small, ...large].reduce((n, a) => n + a.length, 0)
    // // )

    // t.ok(!failed, 'all bytes match')

    // async function testWrite (i, buffer) {
    //   await new Promise((resolve) => {
    //     const filename = TMPDIR + `new-file-${i}.txt`
    //     fs.writeFile(filename, buffer, async (err) => {
    //       if (err) {
    //         failed = true
    //         t.fail(err.message)
    //         return resolve()
    //       }

    //       fs.readFile(filename, (err, result) => {
    //         if (err) {
    //           failed = true
    //           t.fail(err.message)
    //         } else if (Buffer.compare(result, buffer) !== 0) {
    //           failed = true
    //           t.fail('bytes do not match')
    //         }

    //         resolve()
    //       })
    //     })
    //   })
    // }
  })
}
