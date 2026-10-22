import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const available = spawnSync('clang++', ['--version'], { stdio: 'ignore' }).status === 0

test('provider modes preserve access and truncation; ranges preserve unknown length and section bounds', {
  skip: !available ? 'Clang is required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/platform/android/content_resolver.cc', import.meta.url), 'utf8')
  const start = source.indexOf('  int ContentResolver::getOpenMode (')
  const end = source.indexOf('  ContentResolver::ContentResolver (const ContentResolver&', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-provider-ranges-'))
  const filename = path.join(directory, 'provider.cc')
  const executable = path.join(directory, 'provider')
  writeFileSync(filename, `
    #include <algorithm>
    #include <cassert>
    #include <cerrno>
    #include <cstdint>
    #include <fcntl.h>
    #include <limits>
    #include <string>
    #include <sys/types.h>
    using String = std::string;
    struct ContentResolver {
      static int getOpenMode(int, String&);
      static int getReadRange(size_t, size_t, off_t, off_t, int64_t&, size_t&);
    };
    ${source.slice(start, end)}
    int main() {
      String mode;
      assert(ContentResolver::getOpenMode(O_RDONLY, mode) == 0 && mode == "r");
      assert(ContentResolver::getOpenMode(O_WRONLY | O_CREAT | O_TRUNC, mode) == 0 && mode == "wt");
      assert(ContentResolver::getOpenMode(O_RDWR | O_TRUNC, mode) == 0 && mode == "rwt");
      assert(ContentResolver::getOpenMode(O_WRONLY | O_APPEND, mode) == 0 && mode == "wa");
      assert(ContentResolver::getOpenMode(O_RDWR | O_APPEND, mode) == 0 && mode == "rw");
      assert(ContentResolver::getOpenMode(O_WRONLY, mode) == 0 && mode == "rw");
      assert(ContentResolver::getOpenMode(O_RDONLY | O_TRUNC, mode) == EINVAL && mode.empty());
      assert(ContentResolver::getOpenMode(O_RDWR | O_CREAT | O_EXCL, mode) == ENOTSUP && mode.empty());
      int64_t position;
      size_t count;
      constexpr auto sequential = std::numeric_limits<size_t>::max();
      assert(ContentResolver::getReadRange(0, 64, 0, -1, position, count) == 0 && position == 0 && count == 64);
      assert(ContentResolver::getReadRange(sequential, 64, 0, -1, position, count) == 0 && position == -1 && count == 64);
      assert(ContentResolver::getReadRange(4, 64, 100, 8, position, count) == 0 && position == 104 && count == 4);
      assert(ContentResolver::getReadRange(8, 64, 100, 8, position, count) == 0 && count == 0);
      assert(ContentResolver::getReadRange(9, 64, 100, 8, position, count) == 0 && count == 0);
      assert(ContentResolver::getReadRange(0, 64, 0, 0, position, count) == 0 && count == 0);
      assert(ContentResolver::getReadRange(sequential, 64, 100, 8, position, count) == ENOTSUP);
      assert(ContentResolver::getReadRange(0, 64, -1, 8, position, count) == EINVAL);
      assert(ContentResolver::getReadRange(0, 64, 0, -2, position, count) == EINVAL);
      assert(ContentResolver::getReadRange(std::numeric_limits<int64_t>::max(), 64, 1, -1, position, count) == EOVERFLOW);
    }
  `)
  const compiled = spawnSync('clang++', ['-std=c++20', filename, '-o', executable], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})
