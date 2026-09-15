import test from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, writeFileSync } from 'node:fs'
import { spawnSync } from 'node:child_process'
import { tmpdir } from 'node:os'
import path from 'node:path'

const compiler = ['clang++-18', 'clang++', 'c++'].find(command =>
  spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

// Allow the new thread to finish before its creator resumes. This forces an
// immediate child's exit waiter to run before open() returns to its caller.
test('native process readers start before immediate child exits close the pipes', {
  skip: process.platform === 'win32' || !compiler ? 'A Unix C++ compiler is required' : false
}, () => {
  const header = readFileSync(new URL('../../src/runtime/process.hh', import.meta.url), 'utf8')
    .replaceAll(/^#include ".*"\n/gm, '')
  const source = readFileSync(new URL('../../src/runtime/process/unix.cc', import.meta.url), 'utf8')
    .replaceAll(/^#include ".*"\n/gm, '')
  const directory = mkdtempSync(path.join(tmpdir(), 'oro-process-startup-'))
  const filename = path.join(directory, 'startup.cc')
  const executable = path.join(directory, 'startup')
  writeFileSync(filename, `
    #include <array>
    #include <atomic>
    #include <cassert>
    #include <chrono>
    #include <functional>
    #include <memory>
    #include <mutex>
    #include <sstream>
    #include <string>
    #include <thread>
    #include <vector>
    #include <sys/wait.h>
    #include <unistd.h>
    using StringStream = std::stringstream;
    using Lock = std::lock_guard<std::mutex>;
    void msleep (int delay) { std::this_thread::sleep_for(std::chrono::milliseconds(delay)); }
    namespace oro::runtime::types {
      template <typename T, size_t N> using Array = std::array<T, N>;
      template <typename T> using Atomic = std::atomic<T>;
      template <typename T> using Function = std::function<T>;
      template <typename T> using SharedPointer = std::shared_ptr<T>;
      template <typename T> using UniquePointer = std::unique_ptr<T>;
      template <typename T> using Vector = std::vector<T>;
      using String = std::string;
      using Mutex = std::mutex;
      using MessageCallback = std::function<void(const String&)>;
      struct Thread : std::thread {
        Thread () = default;
        Thread (Thread&&) = default;
        Thread& operator= (Thread&&) = default;
        template <typename F> explicit Thread (F fn) : std::thread(fn) { join(); }
        void detach () { if (joinable()) std::thread::detach(); }
      };
    }
    namespace oro::runtime::string {
      std::vector<std::string> splitc (const std::string& value, char delimiter) {
        std::vector<std::string> parts;
        size_t start = 0;
        for (size_t i = 0; i < value.size(); ++i) {
          if (value[i] == delimiter) { parts.push_back(value.substr(start, i - start)); start = i + 1; }
        }
        parts.push_back(value.substr(start));
        return parts;
      }
    }
    ${header}
    ${source}
    int main () {
      using namespace oro::runtime::process;
      ProcessConfig config;
      config.rawOutput = true;
      config.useDirectArguments = true;
      config.argumentCount = 2;
      std::string output, error;
      int exits = 0;
      auto stdoutCallback = [&](const auto& chunk) { output += chunk; };
      auto stderrCallback = [&](const auto& chunk) { error += chunk; };
      auto exitCallback = [&](const auto& code) { assert(code == "0"); ++exits; };
      Process child("/usr/bin/printf", std::string("%s") + char(1) + "A B", "",
        stdoutCallback, stderrCallback, exitCallback, false, config);
      assert(child.open() > 0);
      assert(child.wait() == 0);
      assert(output == "A B");
      assert(error.empty());
      assert(exits == 1);
      output.clear();
      Process forked([]() -> int {
        ::write(STDOUT_FILENO, "out", 3);
        ::write(STDERR_FILENO, "err", 3);
        _exit(0);
      }, stdoutCallback, stderrCallback, exitCallback, false, config);
      assert(forked.wait() == 0);
      assert(output == "out");
      assert(error == "err");
      assert(exits == 2);
    }
  `)
  const compiled = spawnSync(compiler, ['-std=c++20', '-pthread', filename, '-o', executable], { encoding: 'utf8' })
  assert.equal(compiled.status, 0, compiled.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stderr || String(result.error || ''))
})
