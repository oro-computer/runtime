import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const compiler = ['clang++-18', 'clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

test('parallel Android resource requests publish complete cache entries', {
  skip: !compiler ? 'A C++ compiler is required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/filesystem/resource.cc', import.meta.url), 'utf8')
  const access = source.slice(source.indexOf('  bool Resource::startAccessing ()'), source.indexOf('  bool Resource::stopAccessing ()'))
  const read = source.slice(source.indexOf('  const unsigned char* Resource::read (bool cached)'), source.indexOf('  const String Resource::str (bool cached)'))
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-resource-cache-'))
  const filename = path.join(directory, 'cache.cc')
  const executable = path.join(directory, process.platform === 'win32' ? 'cache.exe' : 'cache')
  writeFileSync(filename, `
    #include <atomic>
    #include <barrier>
    #include <cassert>
    #include <cstring>
    #include <filesystem>
    #include <fstream>
    #include <map>
    #include <memory>
    #include <mutex>
    #include <string>
    #include <thread>
    #include <vector>
    #define ORO_RUNTIME_PLATFORM_ANDROID 1
    namespace fs = std::filesystem;
    using Path = fs::path;
    using String = std::string;
    using Lock = std::lock_guard<std::recursive_mutex>;
    std::recursive_mutex mutex;
    void* sharedAndroidAssetManager = reinterpret_cast<void*>(1);
    constexpr int AASSET_MODE_BUFFER = 0;
    struct Asset { String bytes; };
    std::atomic<int> assetReads = 0;
    Path getRelativeAndroidAssetManagerPath (const Path& path) { return path; }
    Asset* AAssetManager_open (void*, const char* path, int) {
      ++assetReads;
      return new Asset{String("export default '") + path + "'"};
    }
    size_t AAsset_getLength (Asset* asset) { return asset->bytes.size(); }
    const void* AAsset_getBuffer (Asset* asset) { return asset->bytes.data(); }
    void AAsset_close (Asset* asset) { delete asset; }
    struct Resource {
      struct Cache { std::shared_ptr<unsigned char[]> bytes; size_t size = 0; } cache;
      struct { bool cache = true; } options;
      Path path;
      bool accessing = false;
      std::shared_ptr<unsigned char[]> bytes;
      bool startAccessing ();
      const unsigned char* read (bool cached = true);
      static Path getResourcesPath () { return {}; }
      static bool isMountedPath (const Path&) { return false; }
      bool exists () { return true; }
    };
    std::map<String, Resource::Cache> caches;
    ${access}
    ${read}
    int main () {
      constexpr int workers = 12, files = 1000;
      std::barrier start(workers);
      std::vector<std::thread> threads;
      for (int worker = 0; worker < workers; ++worker) {
        threads.emplace_back([&, worker] {
          start.arrive_and_wait();
          for (int index = 0; index < files * 4; ++index) {
            const auto name = "oro-cache-asset-" + std::to_string((index + worker * 83) % files) + ".js";
            Resource resource;
            resource.path = name;
            assert(resource.startAccessing());
            const auto bytes = resource.read();
            assert(bytes != nullptr);
            assert(String(reinterpret_cast<const char*>(bytes), resource.cache.size) == "export default '" + name + "'");
          }
        });
      }
      for (auto& thread : threads) thread.join();
      assert(caches.size() == files);
      const int before = assetReads;
      Resource cached;
      cached.path = "oro-cache-asset-0.js";
      assert(cached.startAccessing() && cached.read());
      assert(assetReads == before);
    }
  `)
  const compiled = spawnSync(compiler, ['-std=c++20', '-pthread', filename, '-o', executable], { encoding: 'utf8' })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stderr || String(result.error || result.signal || ''))
})
