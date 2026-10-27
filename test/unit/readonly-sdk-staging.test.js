import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const compiler = ['clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

test('SDK API/header/archive staging leaves read-only inputs intact and produces writable outputs', {
  skip: !compiler || process.platform === 'win32' ? 'Requires a POSIX C++ compiler and permissions' : false
}, () => {
  const source = readFileSync(new URL('../../src/cli/main.cc', import.meta.url), 'utf8')
  const start = source.indexOf('static void copyWritableFile (')
  const end = source.indexOf('\nvoid initializeEnvFile (', start)
  assert.ok(start >= 0 && end > start)
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-readonly-staging-'))
  const filename = path.join(directory, 'staging.cc')
  const executable = path.join(directory, 'staging')
  writeFileSync(filename, `
    #include <cassert>
    #include <filesystem>
    #include <fstream>
    #include <stdexcept>
    namespace fs = std::filesystem;
    using Path = fs::path;
    ${source.slice(start, end)}
    int main() {
      fs::create_directories("sdk/nested");
      fs::create_directories("sdk/node_modules/package");
      fs::create_directories("linked-headers");
      std::ofstream("linked-headers/public.hh") << "public header";
      fs::create_directory_symlink(fs::absolute("linked-headers"), "sdk/header-alias");
      std::ofstream("sdk/nested/api.js") << "public";
      std::ofstream("sdk/node_modules/package/private.txt") << "excluded";
      fs::permissions("sdk/nested/api.js", fs::perms::owner_read);
      fs::permissions("sdk/nested", fs::perms::owner_read | fs::perms::owner_exec);
      fs::permissions("sdk", fs::perms::owner_read | fs::perms::owner_exec);
      copyWritableTree("sdk/", "generated/api", true);
      assert(fs::exists("generated/api/nested/api.js"));
      assert(!fs::exists("generated/api/node_modules"));
      assert(fs::is_regular_file("generated/api/header-alias/public.hh"));
      assert(!fs::is_symlink("generated/api/header-alias"));
      assert((fs::status("sdk/nested/api.js").permissions() & fs::perms::owner_write) == fs::perms::none);
      assert((fs::status("sdk/nested").permissions() & fs::perms::owner_write) == fs::perms::none);
      assert((fs::status("generated/api/nested/api.js").permissions() & fs::perms::owner_write) != fs::perms::none);
      assert((fs::status("generated/api/nested").permissions() & fs::perms::owner_write) != fs::perms::none);
      std::ofstream("generated/api/nested/config.hh") << "public generated header";
      assert(fs::file_size("generated/api/nested/config.hh") > 0);
      fs::permissions("generated/api/nested/api.js", fs::perms::owner_read);
      fs::permissions("generated/api/nested", fs::perms::owner_read | fs::perms::owner_exec);
      copyWritableTree("sdk", "generated/api", true);
      assert((fs::status("generated/api/nested/api.js").permissions() & fs::perms::owner_write) != fs::perms::none);
      copyWritableFile("sdk/nested/api.js", "generated/jni/init.cc");
      fs::permissions("generated/jni/init.cc", fs::perms::owner_read);
      copyWritableFile("sdk/nested/api.js", "generated/jni/init.cc");
      assert((fs::status("generated/jni/init.cc").permissions() & fs::perms::owner_write) != fs::perms::none);
      assert((fs::status("sdk/nested/api.js").permissions() & fs::perms::owner_write) == fs::perms::none);
      bool rejected = false;
      try { copyWritableTree("sdk", "sdk/nested/output"); } catch (const std::runtime_error&) { rejected = true; }
      assert(rejected && !fs::exists("sdk/nested/output"));
      fs::create_directories("other");
      fs::create_directory_symlink(fs::absolute("other"), "generated/alias");
      rejected = false;
      try { copyWritableTree("sdk", "generated/alias", true); } catch (const std::runtime_error&) { rejected = true; }
      assert(rejected && fs::is_empty("other"));
      fs::create_directories("generated/hardlink/nested");
      fs::create_hard_link("sdk/nested/api.js", "generated/hardlink/nested/api.js");
      rejected = false;
      try { copyWritableTree("sdk/", "generated/hardlink", true); } catch (const std::runtime_error&) { rejected = true; }
      assert(rejected);
      assert((fs::status("sdk/nested/api.js").permissions() & fs::perms::owner_write) == fs::perms::none);
      fs::create_directory_symlink(fs::absolute("linked-headers"), "linked-headers/cycle");
      rejected = false;
      try { copyWritableTree("sdk", "generated/cycle-check", true); } catch (const std::runtime_error&) { rejected = true; }
      assert(rejected);
    }
  `)
  const compiled = spawnSync(compiler, ['-std=c++20', filename, '-o', executable], { encoding: 'utf8', timeout: 60000 })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { cwd: directory, encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stderr)
})
