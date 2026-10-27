import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { spawnSync } from 'node:child_process'

const root = fileURLToPath(new URL('../../', import.meta.url))
const compiler = [process.env.ORO_TEST_CXX, 'clang++', 'g++', 'c++'].filter(Boolean).find(command => spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

test('Linux keyring distinguishes lookup errors from absent keys and preserves native key access lifetime', () => {
  assert.ok(compiler)
  const source = fs.readFileSync(path.join(root, 'src/runtime/core/services/secure_storage.cc'), 'utf8')
  const get = source.slice(source.indexOf('  bool SecureStorage::platformGet ('), source.indexOf('  bool SecureStorage::platformRemove ('))
  const linux = get.slice(get.indexOf('#elif ORO_RUNTIME_PLATFORM_LINUX') + '#elif ORO_RUNTIME_PLATFORM_LINUX'.length, get.lastIndexOf('#else'))
  assert.ok(linux.includes('GError* failure') && linux.includes('&failure'))
  const start = source.indexOf('  struct NativeRecordKeyAccess {')
  const end = source.indexOf('  SecureStorage::SecureStorage (', start)
  assert.ok(start > 0 && end > start)
  const keys = source.slice(start, end)
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-native-key-access-'))
  const filename = path.join(directory, 'keys.cc')
  const executable = path.join(directory, process.platform === 'win32' ? 'keys.exe' : 'keys')
  fs.writeFileSync(filename, `
#include "src/runtime/native/records.hh"
#include <cassert>
#include <mutex>
#include <algorithm>
#include <cstring>
using String = std::string;
using gchar = char;
struct GError {};
bool failed = false;
int freed = 0;
int freedResults = 0;
char* lookup(void*, void*, GError** error, ...) {
  if (failed) *error = new GError;
  return nullptr;
}
struct Backend {
  decltype(&lookup) lookup = ::lookup;
  void g_error_free(GError* error) { freed++; delete error; }
  void g_free(void*) { freedResults++; }
};
struct Context { Backend backend; int schema; } context;
Context& libsecretContext() { return context; }
bool ensureLibSecretBackend(Context&, String&) { return true; }
namespace bytes {
struct Buffer : std::vector<unsigned char> {
  using std::vector<unsigned char>::vector;
  static Buffer from(const unsigned char* data,size_t size) { return Buffer(data,data+size); }
  static Buffer from(const std::vector<unsigned char>& value) { return Buffer(value.begin(),value.end()); }
};
namespace base64 { std::vector<unsigned char> decode(const std::string&) { return {}; } }
}
bool platformGet(const String& scope, const String& key, bytes::Buffer& value, bool& found, String& error) {
${linux}
}
namespace oro::runtime::core::services {
struct NativeRecordKeyAccess;
class SecureStorage {
 public:
  bool enabled = true;
  std::map<String, bytes::Buffer> entries;
  String computeDefaultScope() { return "oro://public.native"; }
  bool platformGet(const String&, const String& key, bytes::Buffer& value, bool& found, String&) {
    found = entries.contains(key); if(found) value = entries.at(key); return true;
  }
  bool platformSet(const String&,const String& key,const bytes::Buffer& value,String&) { entries[key]=value; return true; }
  bool platformRemove(const String&,const String& key,String&) { entries.erase(key); return true; }
};
${keys}
}
int main() {
  bool found = true; String error; bytes::Buffer value;
  failed = true;
  assert(!platformGet("public", "public", value, found, error));
  assert(freed == 1 && !error.empty());
  failed = false; found = true; error.clear();
  assert(platformGet("public", "public", value, found, error));
  assert(!found && error.empty());
  using namespace oro::runtime::native;
  using namespace oro::runtime::core::services;
  SecureStorage storage;
  auto access = std::make_shared<NativeRecordKeyAccess>(); access->storage=&storage; access->active=true;
  NativeRecordKeyStore keys(access);
  const std::string scope(64, 'a');
  assert(!keys.get(scope,'a'));
  RecordKey secret{}; secret[0]=42;
  keys.put(scope,'a',secret);
  assert(keys.get(scope,'a')->at(0)==42);
  keys.remove(scope,'a'); assert(!keys.get(scope,'a'));
  access->storage=nullptr;
  try { keys.get(scope,'a'); return 2; } catch(const StateError& error) { assert(error.code=="EACCES"); }
}
`)
  const built = spawnSync(compiler, ['-std=c++20', '-Werror', '-I' + root, '-I' + path.join(root, 'include'), filename, '-o', executable], { encoding: 'utf8', timeout: 30000 })
  assert.equal(built.status, 0, built.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})
