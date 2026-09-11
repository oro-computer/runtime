import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import vm from 'node:vm'

const source = readFileSync(new URL('../../api/sqlite.js', import.meta.url), 'utf8')
const start = source.indexOf('export function hasCRSQLite ()')
const end = source.indexOf('\nexport class Statement', start)

for (const available of [false, true]) {
  test(`hasCRSQLite checks extension functions when availability is ${available}`, () => {
    let closed = 0
    let queries = 0
    const context = vm.createContext({
      OPEN_DEFAULT: 0,
      OPEN_MEMORY: 0,
      Database: class {
        exec (sql) {
          assert.equal(sql, 'SELECT crsql_version()')
          queries++
          if (!available) throw new Error('no such function: crsql_version')
        }

        close () { closed++ }
      }
    })
    const detect = vm.runInContext(`let cachedCRSQLiteSupport; ${source.slice(start, end).replace('export ', '')}; hasCRSQLite`, context)
    assert.equal(detect(), available)
    assert.equal(detect(), available)
    assert.equal(closed, 1, 'closes the probe connection even when the extension is absent')
    assert.equal(queries, 1, 'caches the extension probe')
  })
}

const compiler = ['clang++-18', 'clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const sqliteFlags = spawnSync('pkg-config', ['--cflags', '--libs', 'sqlite3'], { encoding: 'utf8' })

test('packaged iOS SQLite works without ORO_HOME and rejects a broken configured extension', {
  skip: process.platform === 'win32' || !compiler || sqliteFlags.status !== 0
    ? 'A POSIX C++ compiler and SQLite development library are required'
    : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-sqlite-config-'))
  const filename = path.join(directory, 'regression.cc')
  const executable = path.join(directory, 'regression')
  const header = readFileSync(new URL('../../src/runtime/sqlite.hh', import.meta.url), 'utf8')
    .replace('#include "platform.hh"', '')
    .replace('#include "sqlite/sqlite3.h"', '#include <sqlite3.h>')
  const native = readFileSync(new URL('../../src/runtime/sqlite/sqlite.cc', import.meta.url), 'utf8')
    .replace('#include "../sqlite.hh"', '')
  writeFileSync(filename, `
    #include <cassert>
    #include <cstdlib>
    #include <functional>
    #include <string>
    #define ORO_RUNTIME_PLATFORM_IOS 1
    #define ORO_RUNTIME_PLATFORM_IOS_SIMULATOR 1
    namespace oro::runtime {
      using String = std::string;
      template <typename T> using Function = std::function<T>;
    }
    ${header}
    ${native}
    int main () {
      unsetenv("ORO_HOME");
      oro::runtime::sqlite::Database database;
      assert(database.open(":memory:"));
      assert(database.exec("CREATE TABLE entries (value TEXT)"));
      assert(database.exec("INSERT INTO entries VALUES ('mobile')"));
      int rows = 0;
      assert(database.exec("SELECT value FROM entries", [&](int, char** values, char**) {
        assert(std::string(values[0]) == "mobile");
        ++rows;
        return 0;
      }));
      assert(rows == 1);
      assert(!database.exec("SELECT crsql_version()"));
      assert(database.close());
      setenv("ORO_HOME", "/nonexistent/oro-sqlite-regression", 1);
      assert(!database.open(":memory:"));
      assert(database.lastResult() != SQLITE_OK);
    }
  `)
  const compiled = spawnSync(compiler, ['-std=c++20', filename, '-o', executable, ...sqliteFlags.stdout.trim().split(/\s+/)], { encoding: 'utf8' })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8' })
  assert.equal(result.status, 0, result.stderr)
})
