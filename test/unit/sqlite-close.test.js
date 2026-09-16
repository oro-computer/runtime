import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const compiler = ['clang++-18', 'clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const sqliteFlags = spawnSync('pkg-config', ['--cflags', '--libs', 'sqlite3'], { encoding: 'utf8' })

test('SQLite close releases extension statements and preserves deferred application statements', {
  skip: process.platform !== 'linux' || !compiler || sqliteFlags.status !== 0
    ? 'A Linux C++ compiler and SQLite development library are required'
    : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-sqlite-close-'))
  const extensionDirectory = path.join(directory, 'lib', 'test-desktop', 'extensions')
  mkdirSync(extensionDirectory, { recursive: true })
  const extensionSource = path.join(directory, 'extension.cc')
  // Model CR-SQLite's cached statement, which requires explicit teardown before close_v2.
  writeFileSync(extensionSource, `
    #include <cassert>
    #include <sqlite3ext.h>
    SQLITE_EXTENSION_INIT1
    struct State {
      sqlite3_stmt* cached = nullptr;
      bool fail = false;
    };
    extern "C" int sqlite3_crsqlite_init (sqlite3* db, char**, const sqlite3_api_routines* api) {
      SQLITE_EXTENSION_INIT2(api);
      auto* state = new State;
      assert(sqlite3_prepare_v2(db, "SELECT 1", -1, &state->cached, nullptr) == SQLITE_OK);
      assert(sqlite3_create_function_v2(db, "crsql_test_fail", 1, SQLITE_UTF8, state,
        [](sqlite3_context* context, int, sqlite3_value** values) {
          static_cast<State*>(sqlite3_user_data(context))->fail = sqlite3_value_int(values[0]);
        }, nullptr, nullptr, nullptr) == SQLITE_OK);
      return sqlite3_create_function_v2(db, "crsql_finalize", 0, SQLITE_UTF8, state,
        [](sqlite3_context* context, int, sqlite3_value**) {
          auto* state = static_cast<State*>(sqlite3_user_data(context));
          if (state->fail) {
            sqlite3_result_error(context, "extension teardown failed", -1);
            return;
          }
          sqlite3_finalize(state->cached);
          state->cached = nullptr;
        }, nullptr, nullptr, [](void* data) {
          auto* state = static_cast<State*>(data);
          assert(state->cached == nullptr);
          delete state;
        });
    }
  `)
  const flags = sqliteFlags.stdout.trim().split(/\s+/)
  const extension = spawnSync(compiler, [
    '-std=c++20', '-shared', '-fPIC', extensionSource,
    '-o', path.join(extensionDirectory, 'crsqlite.so'), ...flags
  ], { encoding: 'utf8' })
  assert.equal(extension.status, 0, extension.stderr)

  const header = readFileSync(new URL('../../src/runtime/sqlite.hh', import.meta.url), 'utf8')
    .replace('#include "platform.hh"', '')
    .replace('#include "sqlite/sqlite3.h"', '#include <sqlite3.h>')
  const native = readFileSync(new URL('../../src/runtime/sqlite/sqlite.cc', import.meta.url), 'utf8')
    .replace('#include "../sqlite.hh"', '')
  const filename = path.join(directory, 'regression.cc')
  const executable = path.join(directory, 'regression')
  writeFileSync(filename, `
    #include <cassert>
    #include <cstdlib>
    #include <functional>
    #include <string>
    #include <utility>
    #define ORO_RUNTIME_PLATFORM_DESKTOP 1
    namespace oro::runtime {
      using String = std::string;
      template <typename T> using Function = std::function<T>;
      struct { String arch = "test"; } platform;
    }
    ${header}
    ${native}
    using oro::runtime::sqlite::Database;
    using oro::runtime::sqlite::Statement;
    int closed = 0;
    void observeClose (Database& db) {
      assert(sqlite3_create_function_v2(db.handle(), "close_observer", 0, SQLITE_UTF8,
        nullptr, [](sqlite3_context*, int, sqlite3_value**) {}, nullptr, nullptr,
        [](void*) { ++closed; }) == SQLITE_OK);
    }
    int main (int argc, char** argv) {
      assert(argc == 2);
      unsetenv("ORO_HOME");
      Database plain;
      assert(plain.open(":memory:"));
      observeClose(plain);
      assert(plain.close());
      assert(closed == 1);

      setenv("ORO_HOME", argv[1], 1);
      const std::string filename = std::string(argv[1]) + "/database.db";
      Database db;
      assert(db.open(filename));
      observeClose(db);
      assert(db.exec("CREATE TABLE entries (value TEXT)"));
      assert(db.exec("INSERT INTO entries VALUES ('persisted')"));
      assert(db.close());
      assert(closed == 2);
      assert(db.close());
      assert(closed == 2);
      assert(db.open(filename));
      observeClose(db);
      assert(db.exec("SELECT value FROM entries", [](int, char** values, char**) {
        assert(std::string(values[0]) == "persisted");
        return 0;
      }));
      Database moved(std::move(db));
      assert(!db.isOpen());
      assert(moved.close());
      assert(closed == 3);

      assert(db.open(filename));
      observeClose(db);
      assert(moved.open(":memory:"));
      observeClose(moved);
      moved = std::move(db);
      assert(closed == 4);
      assert(!db.isOpen());
      assert(moved.close());
      assert(closed == 5);
      {
        Database scoped(filename);
        assert(scoped.isOpen());
        observeClose(scoped);
      }
      assert(closed == 6);

      assert(db.open(filename));
      observeClose(db);
      assert(db.open(filename));
      assert(closed == 7);
      observeClose(db);
      assert(db.exec("SELECT crsql_test_fail(1)"));
      assert(!db.close());
      assert(db.lastResult() == SQLITE_ERROR);
      assert(db.isOpen());
      assert(closed == 7);
      assert(!db.open(":memory:"));
      assert(db.exec("SELECT crsql_test_fail(0)"));
      assert(db.close());
      assert(closed == 8);

      assert(db.open(filename));
      observeClose(db);
      Statement pending(db, "SELECT value FROM entries");
      assert(db.close());
      assert(closed == 8);
      pending.finalize();
      assert(closed == 9);
    }
  `)
  const compiled = spawnSync(compiler, ['-std=c++20', filename, '-o', executable, ...flags], { encoding: 'utf8' })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [directory], { encoding: 'utf8' })
  assert.equal(result.status, 0, result.stderr)
})
