import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const compiler = ['clang++-18', 'clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

test('Android packaged files are not treated as directories', {
  skip: !compiler ? 'A C++ compiler is required' : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-android-asset-type-'))
  const source = readFileSync(new URL('../../src/runtime/filesystem/resource.cc', import.meta.url), 'utf8')
  const start = source.indexOf('  bool Resource::isFile (const Path& resourcePath) {')
  const end = source.indexOf('  bool Resource::isMountedPath (', start)
  assert.ok(start >= 0 && end > start)

  // Compile the production predicates with the NDK's asset-handle behavior:
  // openDir returns an allocated handle even when its argument names a file.
  const fixture = `
    #include <cassert>
    #include <filesystem>
    #include <fstream>
    #include <string>
    #define ORO_RUNTIME_PLATFORM_ANDROID 1
    namespace fs = std::filesystem;
    using Path = fs::path;
    using String = std::string;
    struct AAssetManager {};
    struct AAsset {};
    struct AAssetDir {};
    constexpr int AASSET_MODE_BUFFER = 3;
    AAssetManager manager;
    AAssetManager* sharedAndroidAssetManager = &manager;
    int openAssets = 0;
    int openDirectories = 0;
    AAsset* AAssetManager_open (AAssetManager*, const char* name, int) {
      if (String(name) == "index.html" || String(name) == "oro/test.js") {
        ++openAssets;
        return new AAsset;
      }
      return nullptr;
    }
    void AAsset_close (AAsset* asset) { --openAssets; delete asset; }
    AAssetDir* AAssetManager_openDir (AAssetManager*, const char*) {
      ++openDirectories;
      return new AAssetDir;
    }
    void AAssetDir_close (AAssetDir* directory) { --openDirectories; delete directory; }
    Path getRelativeAndroidAssetManagerPath (const Path& resourcePath) { return resourcePath; }
    struct Resource {
      static bool isFile (const Path&);
      static bool isDirectory (const Path&);
      static bool isDirectory (const String&);
    };
    ${source.slice(start, end)}
    int main () {
      assert(Resource::isFile(Path("index.html")));
      assert(!Resource::isDirectory(Path("index.html")));
      assert(!Resource::isDirectory(Path("oro/test.js")));
      assert(Resource::isDirectory(Path("oro")));
      assert(Resource::isDirectory(Path("")));
      std::ofstream("loose.txt") << "fixture";
      assert(!Resource::isDirectory(Path("loose.txt")));
      sharedAndroidAssetManager = nullptr;
      assert(Resource::isDirectory(Path(".")));
      assert(!Resource::isDirectory(Path("missing")));
      assert(openAssets == 0);
      assert(openDirectories == 0);
    }
  `
  const filename = path.join(directory, 'asset-type.cc')
  const executable = path.join(directory, process.platform === 'win32' ? 'asset-type.exe' : 'asset-type')
  writeFileSync(filename, fixture)
  const compiled = spawnSync(compiler, ['-std=c++20', filename, '-o', executable], { encoding: 'utf8' })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { cwd: directory, encoding: 'utf8' })
  assert.equal(result.status, 0, result.stderr)
})
