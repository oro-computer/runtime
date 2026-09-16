import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'
import vm from 'node:vm'

const read = file => readFileSync(new URL(`../../${file}`, import.meta.url), 'utf8')
const compiler = ['clang++-18', 'clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

test('filesystem errors retain libuv names through IPC and tar extraction checks', {
  skip: !compiler ? 'A C++ compiler is required' : false
}, async () => {
  const source = read('src/runtime/core/services/fs.cc')
  const start = source.indexOf('  void FS::lstat (')
  const end = source.indexOf('  void FS::link (', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-fs-errors-'))
  const filename = path.join(directory, 'regression.cc')
  const executable = path.join(directory, process.platform === 'win32' ? 'regression.exe' : 'regression')
  writeFileSync(filename, `
    #include <cassert>
    #include <filesystem>
    #include <functional>
    #include <iostream>
    #include <map>
    #include <memory>
    #include <string>
    using String = std::string;
    struct Value {
      using Entries = std::map<String, Value>;
      Entries entries;
      String text;
      long number = 0;
      Value () = default;
      Value (const char* value) : text(value) {}
      Value (String value) : text(value) {}
      Value (long value) : number(value) {}
      Value (Entries value) : entries(value) {}
    };
    namespace JSON { using Object = Value; }
    struct QueuedResponse {};
    struct uv_fs_t { void* data = nullptr; long result = 0; };
    bool immediate = false;
    int failure = -4058;
    const char* uv_err_name (int code) {
      switch (code) {
        case -2: case -4058: return "ENOENT";
        case -13: case -4092: return "EACCES";
        default: return "ELOOP";
      }
    }
    const char* uv_strerror (int code) { return uv_err_name(code); }
    long uv_fs_get_result (uv_fs_t* req) { return req->result; }
    void* uv_fs_get_statbuf (uv_fs_t*) { return nullptr; }
    int uv_fs_lstat (void*, uv_fs_t* req, const char*, void (*callback)(uv_fs_t*)) {
      if (immediate) return failure;
      req->result = failure;
      callback(req);
      return 0;
    }
    JSON::Object getStatsJSON (const String&, void*) { return {}; }
    struct FS {
      using Callback = std::function<void(const String&, JSON::Object, QueuedResponse)>;
      struct Descriptor {
        struct { std::filesystem::path path; } resource;
        Descriptor (FS*, int, String path) { resource.path = path; }
      };
      struct RequestContext {
        String seq;
        Callback callback;
        uv_fs_t req;
        RequestContext (std::shared_ptr<Descriptor>, String seq, Callback callback)
          : seq(seq), callback(callback) { req.data = this; }
      };
      struct {
        void* get () { return nullptr; }
        void dispatch (std::function<void()> callback) { callback(); }
      } loop;
      void lstat (const String&, const String&, const Callback);
    };
    ${source.slice(start, end)}
    int main () {
      FS fs;
      for (const int code : {-2, -4058, -13, -4092, -40, -4067}) {
        failure = code;
        for (const bool mode : {false, true}) {
          immediate = mode;
          int calls = 0;
          fs.lstat("seq", "destination", [&](const String& seq, JSON::Object result, QueuedResponse) {
            ++calls;
            assert(seq == "seq");
            assert(result.entries.at("source").text == "fs.lstat");
            const auto error = result.entries.at("err").entries;
            assert(error.at("code").number == code);
            assert(error.at("name").text == uv_err_name(code));
            std::cout << code << " " << error.at("name").text << "\\n";
          });
          assert(calls == 1);
        }
      }
    }
  `)
  const compiled = spawnSync(compiler, ['-std=c++20', filename, '-o', executable], { encoding: 'utf8' })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)

  const ipc = read('api/ipc.js')
  const tar = read('api/tar.js')
  let failure
  const context = vm.createContext({
    getErrorClass: () => Error,
    InternalError: Error,
    TimeoutError: Error,
    ErrnoError: { errno: { constants: {}, strings: {} } },
    fs: { promises: { async lstat () { throw failure } } }
  })
  const makeError = vm.runInContext(
    ipc.slice(ipc.indexOf('export function maybeMakeError ('), ipc.indexOf('\n/**\n * Represents an OK IPC status.'))
      .replace('export ', '') + '\nmaybeMakeError', context)
  const lstatMaybe = vm.runInContext(
    tar.slice(tar.indexOf('async function lstatMaybe ('), tar.indexOf('\nasync function ensureExtractPath (')) + '\nlstatMaybe', context)
  for (const line of result.stdout.trim().split('\n')) {
    const [code, name] = line.split(' ')
    failure = makeError({ code: Number(code), name, message: name })
    assert.equal(failure.code, Number(code), 'numeric error code is preserved')
    assert.equal(failure.name, name, 'libuv name survives IPC decoding')
    if (name === 'ENOENT') {
      assert.equal(await lstatMaybe('destination'), null, 'tar can create a missing destination')
    } else {
      await assert.rejects(lstatMaybe('destination'), error => error === failure,
        'tar propagates permission and symlink errors')
    }
  }
})
