import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { spawnSync } from 'node:child_process'

const root = fileURLToPath(new URL('../../', import.meta.url))
const compiler = [process.env.ORO_TEST_CXX, 'clang++', 'g++', 'c++'].filter(Boolean).find(command => spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0)

test('LLM route metrics read configuration after startup and initialize once across threads', () => {
  assert.ok(compiler, 'Requires a C++20 compiler')
  const server = fs.readFileSync(path.join(root, 'src/runtime/ai/server.cc'), 'utf8')
  const start = server.indexOf('  struct RouteMetrics {')
  const end = server.indexOf('  static bool send_error_response(', start)
  assert.ok(start > 0 && end > start)
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'oro-route-metrics-'))
  const filename = path.join(directory, 'metrics.cc')
  const executable = path.join(directory, process.platform === 'win32' ? 'metrics.exe' : 'metrics')
  fs.writeFileSync(filename, `
#include <atomic>
#include <cassert>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using String = std::string;
using Mutex = std::recursive_mutex;
using Lock = std::lock_guard<Mutex>;
template <typename K, typename V> using Map = std::map<K, V>;
template <typename T> using Vector = std::vector<T>;
bool started = false;
std::atomic<int> configurationReads = 0;
namespace oro::runtime::config {
  Map<String, String> getUserConfig () {
    assert(started && "Configuration was accessed before main");
    configurationReads++;
    return {{"ai_llm_debug_route_metrics_recent_ms", "1234"}};
  }
}
bool route_metrics_enabled () { return true; }
long long nowMsSteady () { return 100; }
${server.slice(start, end)}
int main () {
  assert(configurationReads == 0);
  started = true;
  Vector<std::thread> workers;
  for (int index = 0; index < 8; index++) {
    workers.emplace_back([] { routeMetrics().recordStatus("/health", 200); });
  }
  for (auto& worker : workers) worker.join();
  assert(configurationReads == 1);
  assert(routeMetrics().recentWindowMs == 1234);
  routeMetrics().record("/health");
  Map<String, uint64_t> counts;
  Vector<String> recent;
  Map<String, Map<String, uint64_t>> status;
  routeMetrics().snapshot(counts, recent, status);
  assert(counts.at("/health") == 1 && recent.size() == 1);
  assert(status.at("/health").at("2xx") == 8);
}
`)
  const built = spawnSync(compiler, ['-std=c++20', '-Wall', '-Werror', '-pthread', filename, '-o', executable], { encoding: 'utf8', timeout: 60000 })
  assert.equal(built.status, 0, built.stdout + built.stderr)
  const result = spawnSync(executable, [], { encoding: 'utf8', timeout: 10000 })
  assert.equal(result.status, 0, result.stdout + result.stderr)
})
