import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const compiler = ['clang++-18', 'clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)
const source = readFileSync(new URL('../../src/cli/main.cc', import.meta.url), 'utf8')
const start = source.indexOf('#if ORO_RUNTIME_HAS_IROH_FFI\n      const auto staticIroh')
const end = source.indexOf('#endif', start) + '#endif'.length
assert.ok(start >= 0 && end > start)

test('Windows consumer linkage quotes the packaged Iroh archive and rejects missing assets', {
  skip: !compiler ? 'A C++ compiler is required' : false
}, () => {
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-windows-iroh-'))
  const filename = path.join(directory, 'link.cc')
  const executable = path.join(directory, process.platform === 'win32' ? 'link.exe' : 'link')
  // Exercise the CLI's production archive selection using real paths containing spaces.
  writeFileSync(filename, `
    #include <cassert>
    #include <filesystem>
    #include <fstream>
    #include <string>
    namespace fs = std::filesystem;
    int main (int argc, char** argv) {
      assert(argc == 2);
      const fs::path root = fs::path(argv[1]) / "consumer prefix with spaces";
      const auto prefixPath = [&](const std::string& value) { return root / value; };
      const auto quoteBuildPath = [](const fs::path& value) { return std::string(1, '"') + value.string() + '"'; };
      struct { std::string arch = "x86_64"; } platform;
      for (const std::string d : {std::string(""), std::string("d")}) {
        for (const bool present : {false, true}) {
          const auto archive = root / ("lib" + d) / "x86_64-desktop" / "oro_iroh.lib";
          if (present) {
            fs::create_directories(archive.parent_path());
            std::ofstream(archive) << "archive fixture";
          }
          bool missing_assets = false;
          std::string files;
          std::string error;
          const auto logError = [&](const std::string& message) { error = message; };
          ${source.slice(start, end)}
          if (present) {
            assert(!missing_assets);
            assert(error.empty());
            assert(files == quoteBuildPath(archive) + " ");
          } else {
            assert(missing_assets);
            assert(files.empty());
            assert(error.find(archive.string()) != std::string::npos);
          }
        }
      }
    }
  `)
  const compilation = spawnSync(compiler, ['-std=c++20', '-DORO_RUNTIME_HAS_IROH_FFI=1', filename, '-o', executable], { encoding: 'utf8' })
  assert.equal(compilation.status, 0, compilation.stderr)
  const result = spawnSync(executable, [directory], { encoding: 'utf8' })
  assert.equal(result.status, 0, result.stderr)
})
