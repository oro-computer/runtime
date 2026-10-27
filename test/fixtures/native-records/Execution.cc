#include "src/runtime/native/execution.hh"

#include <cassert>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

using namespace oro::runtime::native;

struct PublicKeys : RecordKeyStore {
  std::mutex mutex;
  std::map<std::string, RecordKey> keys;
  std::atomic<bool> available = true;
  std::optional<RecordKey> get (const std::string& scope, char slot) override {
    if (!available) throw StateError("EACCES");
    std::lock_guard lock(mutex);
    const auto found = keys.find(scope + slot);
    return found == keys.end() ? std::optional<RecordKey>() : found->second;
  }
  void put (const std::string& scope, char slot, const RecordKey& key) override {
    std::lock_guard lock(mutex);
    keys[scope + slot] = key;
  }
  void remove (const std::string& scope, char slot) override {
    std::lock_guard lock(mutex);
    keys.erase(scope + slot);
  }
};

template <class F> void fails (const std::string& code, F callback) {
  try { callback(); assert(false); } catch (const StateError& error) { assert(error.code == code); }
}

template <class F> void until (F ready) {
  const auto expires = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!ready()) {
    assert(std::chrono::steady_clock::now() < expires);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

int main (int argc, char** argv) {
  assert(argc == 2);
  auto keys = std::make_shared<PublicKeys>();
  auto lease = std::make_shared<int>(1);
  RecordStore store(std::filesystem::path(argv[1]) / "execution.db", keys, "execution-test", {"authority", "account", "identity"});
  ExecutionController jobs(store, false, lease, [](const auto&) { return true; });
  ExecutionTask task{"public", "public.read", "authority", "account", "target", "operation"};
  std::atomic<int> calls = 0;
  jobs.registerExecutor("public.read", [&](const auto&, const auto&) { calls++; return ExecutionOutcome::Completed; });
  fails("EEXIST", [&] { jobs.registerExecutor("public.read", [](const auto&, const auto&) { return ExecutionOutcome::Unknown; }); });
  fails("EINVAL", [&] { jobs.start(task, false); });
  fails("EINVAL", [&] { jobs.start(task, true, 999); });
  const auto first = jobs.start(task, true);
  until([&] { return !jobs.busy(); });
  assert(jobs.status(task.id).phase == "completed");
  assert(jobs.start(task, true).generation == first.generation && calls == 1);
  fails("ESTALE", [&] { jobs.stop(task.id, "old-generation"); });
  auto changed = task;
  changed.accountRef = "another-account";
  fails("EEXIST", [&] { jobs.start(changed, true); });
  jobs.release(task.id, first.generation);

  std::atomic<bool> leave = false;
  std::atomic<bool> cancelled = false;
  jobs.registerExecutor("public.held", [&](const auto&, const auto& signal) {
    while (!leave) {
      if (signal.cancelled()) cancelled = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return ExecutionOutcome::Completed;
  });
  task.kind = "public.held";
  const auto held = jobs.start(task, true, 1000);
  until([&] { return jobs.status(task.id).phase == "unknown"; });
  until([&] { return cancelled.load(); });
  assert(jobs.busy());
  fails("EBUSY", [&] { jobs.release(task.id, held.generation); });
  auto other = task;
  other.id = "other";
  fails("EBUSY", [&] { jobs.start(other, true); });
  leave = true;
  until([&] { return !jobs.busy(); });
  assert(jobs.status(task.id).outcome == "unknown");
  jobs.release(task.id, held.generation);

  ExecutionController rejected(RecordStore(std::filesystem::path(argv[1]) / "rejected.db", keys,
    "execution-test", {"authority", "account", "identity"}), false, lease, [](const auto&) { return false; });
  rejected.registerExecutor("public.read", [&](const auto&, const auto&) { calls++; return ExecutionOutcome::Completed; });
  task.kind = "public.read";
  assert(rejected.start(task, true).outcome == "not-started" && calls == 1 && !rejected.busy());

  RecordStore continuousStore(std::filesystem::path(argv[1]) / "continuous.db", keys,
    "execution-test", {"authority", "account", "identity"});
  std::atomic<bool> entered = false;
  {
    ExecutionController monitor(continuousStore, true, lease, [](const auto&) { return true; });
    monitor.registerExecutor("public.read", [&](const auto&, const auto& signal) {
      entered = true;
      until([&] { return signal.cancelled(); });
      return ExecutionOutcome::Completed;
    });
    fails("EINVAL", [&] { monitor.start(task, true, 1000); });
    const auto active = monitor.start(task, true, 0);
    until([&] { return entered.load(); });
    const auto interrupt = monitor.cancellationHandler();
    interrupt("obsolete-generation");
    assert(monitor.busy());
    interrupt(active.generation);
    until([&] { return !monitor.busy(); });
    assert(monitor.status(task.id).phase == "unknown");
    monitor.release(task.id, active.generation);
    entered = false;
    monitor.start(task, true, 0);
    until([&] { return entered.load(); });
    monitor.shutdown();
    until([&] { return !monitor.busy(); });
    assert(monitor.status(task.id).phase == "unknown");
    fails("ESHUTDOWN", [&] { monitor.start(other, true, 0); });
  }
  ExecutionController reopened(continuousStore, true, lease, [](const auto&) { return true; });
  assert(reopened.status(task.id).outcome == "unknown");
  assert(!reopened.busy());
  assert(calls == 1);
  std::atomic<bool> cleaning = false;
  std::atomic<bool> cleaned = false;
  ExecutionController cleanup(RecordStore(std::filesystem::path(argv[1]) / "cleanup.db", keys,
    "execution-test", {"authority", "account", "identity"}), false, lease, [](const auto&) { return true; },
    [&](const auto&) {
      cleaning = true;
      until([&] { return cleaned.load(); });
    });
  cleanup.registerExecutor("public.read", [](const auto&, const auto&) { return ExecutionOutcome::Completed; });
  const auto ending = cleanup.start(task, true);
  until([&] { return cleaning.load(); });
  assert(cleanup.busy());
  fails("EBUSY", [&] { cleanup.release(task.id, ending.generation); });
  fails("EBUSY", [&] { cleanup.start(other, true); });
  cleaned = true;
  cleanup.drain();
  assert(!cleanup.busy());
  int removed = 0;
  ExecutionController failed(RecordStore(std::filesystem::path(argv[1]) / "admission-failure.db", keys,
    "execution-test", {"authority", "account", "identity"}), false, lease,
    [&](const auto&) { keys->available = false; return true; }, [&](const auto&) { removed++; });
  failed.registerExecutor("public.read", [](const auto&, const auto&) { assert(false); return ExecutionOutcome::Unknown; });
  fails("EACCES", [&] { failed.start(task, true); });
  keys->available = true;
  assert(removed == 1 && !failed.busy());
  assert(failed.status(task.id).phase == "unknown");
  std::cout << "native-execution-cancellation-quarantine-restart-passed\n";
}
