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
    #include <algorithm>
    #include <atomic>
    #include <barrier>
    #include <cassert>
    #include <cstring>
    #include <filesystem>
    #include <fstream>
    #include <limits>
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
    constexpr int AASSET_MODE_STREAMING = 2;
    struct AAsset { String bytes; size_t offset = 0; };
    std::atomic<int> assetReads = 0;
    Path getRelativeAndroidAssetManagerPath (const Path& path) { return path; }
    AAsset* AAssetManager_open (void*, const char* path, int) {
      ++assetReads;
      return new AAsset{String("export default '") + path + "'"};
    }
    int64_t AAsset_getLength64 (AAsset* asset) { return asset->bytes.size(); }
    int AAsset_read (AAsset* asset, void* output, size_t count) {
      const auto size = std::min<size_t>({count, asset->bytes.size() - asset->offset, 7});
      memcpy(output, asset->bytes.data() + asset->offset, size);
      asset->offset += size;
      return size;
    }
    void AAsset_close (AAsset* asset) { delete asset; }
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

test('Android resource reads handle empty, incomplete and unavailable files', {
  skip: !compiler ? 'A C++ compiler is required' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/filesystem/resource.cc', import.meta.url), 'utf8')
  const size = source.slice(source.indexOf('  size_t Resource::size (bool cached)'), source.indexOf('  const unsigned char* Resource::read () const'))
  const read = source.slice(source.indexOf('  const unsigned char* Resource::read (bool cached)'), source.indexOf('  const String Resource::str (bool cached)'))
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-resource-read-'))
  const filename = path.join(directory, 'read.cc')
  const executable = path.join(directory, process.platform === 'win32' ? 'read.exe' : 'read')
  writeFileSync(filename, `
    #include <algorithm>
    #include <cassert>
    #include <cstring>
    #include <filesystem>
    #include <fstream>
    #include <limits>
    #include <map>
    #include <memory>
    #include <mutex>
    #include <string>
    #define ORO_RUNTIME_PLATFORM_ANDROID 1
    namespace fs = std::filesystem;
    using Path = fs::path;
    using String = std::string;
    using Lock = std::lock_guard<std::recursive_mutex>;
    std::recursive_mutex mutex;
    void* sharedAndroidAssetManager = reinterpret_cast<void*>(1);
    constexpr int AASSET_MODE_BUFFER = 3, AASSET_MODE_STREAMING = 2;
    String payload = String(150000, 'x') + String("a\\0b", 3);
    struct AAsset { String name; size_t offset = 0; };
    int opens = 0, closes = 0, reads = 0;
    Path getRelativeAndroidAssetManagerPath (const Path& path) { return path; }
    AAsset* AAssetManager_open (void*, const char* name, int) {
      if (String(name) == "missing.js" || String(name) == "unavailable.js") return nullptr;
      ++opens;
      return new AAsset{name};
    }
    int64_t AAsset_getLength64 (AAsset* asset) {
      if (asset->name == "empty.js") return 0;
      return payload.size();
    }
    int64_t AAsset_getLength (AAsset* asset) { return AAsset_getLength64(asset); }
    const void* AAsset_getBuffer (AAsset*) { return nullptr; }
    int AAsset_read (AAsset* asset, void* output, size_t count) {
      ++reads;
      if (asset->name == "error.js" && asset->offset > 0) return -1;
      const auto available = payload.size() - (asset->name == "truncated.js" ? 1 : 0);
      const auto size = std::min<size_t>({count, available - asset->offset, 1023});
      memcpy(output, payload.data() + asset->offset, size);
      asset->offset += size;
      return size;
    }
    void AAsset_close (AAsset* asset) { ++closes; delete asset; }
    struct ContentResolver {
      int openFileDescriptor (const String&, off_t* = nullptr, off_t* = nullptr) { return 0; }
      bool closeFileDescriptor (int) { return true; }
      int64_t getFileDescriptorLength (int) { return -1; }
    };
    struct Resource {
      struct Cache { std::shared_ptr<unsigned char[]> bytes; size_t size = 0; } cache;
      struct { bool cache = true; ContentResolver* contentResolver = nullptr; } options;
      struct { String scheme; String str () { return {}; } } url;
      Path path;
      bool accessing = true;
      std::shared_ptr<unsigned char[]> bytes;
      size_t size (bool cached = false) noexcept;
      const unsigned char* read (bool cached = false);
      // Let the read reach the fallback even if the file disappeared after exists().
      bool exists () { return true; }
      bool isAndroidLocalAsset () { return !fs::exists(path); }
      bool isAndroidContent () { return false; }
    };
    std::map<String, Resource::Cache> caches;
    ${size}
    ${read}
    int main () {
      for (const String name : {"empty.js", "module.js"}) {
        Resource resource;
        resource.path = name;
        const auto length = resource.size();
        const auto data = resource.read();
        assert(data != nullptr && resource.cache.size == length);
        assert(String(reinterpret_cast<const char*>(data), length) == (name == "empty.js" ? "" : payload));
        const auto before = reads;
        assert(resource.read(true) == data && reads == before);
        assert(opens == closes);
      }
      for (const String name : {"truncated.js", "error.js", "missing.js", "unavailable.js"}) {
        Resource resource;
        resource.path = name;
        assert(resource.read() == nullptr);
        assert(!caches.contains(name) && !resource.cache.bytes && opens == closes);
      }
      Resource missing;
      missing.path = "missing.js";
      missing.cache.size = 123;
      assert(missing.size() == 0);
      assert(missing.read() == nullptr);
      for (const String name : {"empty-local.js", "local.js"}) {
        const auto content = name == "empty-local.js" ? String() : payload;
        { std::ofstream file(name, std::ios::binary); file.write(content.data(), content.size()); }
        Resource resource;
        resource.path = name;
        const auto before = opens;
        assert(resource.size() == content.size());
        const auto data = resource.read();
        assert(data != nullptr && resource.cache.size == content.size());
        assert(String(reinterpret_cast<const char*>(data), resource.cache.size) == content);
        assert(opens == before);
      }
      sharedAndroidAssetManager = nullptr;
      assert(missing.size() == 0 && missing.read() == nullptr);
      Resource directory;
      directory.path = ".";
      assert(directory.size() == 0 && directory.read() == nullptr);
    }
  `)
  const compiled = spawnSync(compiler, ['-std=c++20', '-pthread', filename, '-o', executable], { encoding: 'utf8' })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { cwd: directory, encoding: 'utf8', timeout: 15000 })
  assert.equal(result.status, 0, result.stderr || String(result.error || result.signal || ''))
})

test('native helpers can select an app resource root after executable-path caching without accepting invalid paths', {
  skip: process.platform !== 'linux' || !compiler ? 'Requires Linux and a C++ compiler' : false
}, () => {
  const source = readFileSync(new URL('../../src/runtime/filesystem/resource.cc', import.meta.url), 'utf8')
  const methods = source.slice(source.indexOf('  bool Resource::setResourcesPath ('), source.indexOf('  Path Resource::getResourcePath ('))
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-resource-root-'))
  const filename = path.join(directory, 'root.cc')
  const executable = path.join(directory, 'root')
  writeFileSync(filename, `
    #include <algorithm>
    #include <cassert>
    #include <filesystem>
    #include <fstream>
    #include <mutex>
    #include <string>
    #include <thread>
    #include <vector>
    #define ORO_RUNTIME_PLATFORM_LINUX 1
    namespace fs = std::filesystem;
    using Path = fs::path;
    using String = std::string;
    using Lock = std::lock_guard<std::recursive_mutex>;
    std::recursive_mutex mutex;
    Path configuredResourcesPath;
    struct Resource {
      static bool setResourcesPath(const Path&);
      static Path getResourcesPath();
    };
    ${methods}
    int main () {
      const auto original = Resource::getResourcesPath();
      const auto resources = original / "app resources";
      fs::create_directory(resources);
      std::ofstream(resources / "index.html") << "public resource";
      assert(!Resource::setResourcesPath("relative"));
      assert(Resource::getResourcesPath() == original);
      assert(Resource::setResourcesPath(resources / "."));
      assert(Resource::getResourcesPath() == fs::canonical(resources));
      assert(!Resource::setResourcesPath(resources / "missing"));
      assert(!Resource::setResourcesPath(resources / "index.html"));
      assert(Resource::getResourcesPath() == fs::canonical(resources));
      std::vector<std::thread> readers;
      for (int index = 0; index < 8; ++index) readers.emplace_back([&] {
        for (int request = 0; request < 1000; ++request) assert(Resource::getResourcesPath() == fs::canonical(resources));
      });
      for (auto& reader : readers) reader.join();
    }
  `)
  const compiled = spawnSync(compiler, ['-std=c++20', '-pthread', filename, '-o', executable], { encoding: 'utf8' })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr)
})
