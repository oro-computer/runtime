#include "src/runtime/native/deferred.hh"

#include <cassert>
#include <thread>
#include <iostream>

extern "C" {
  const unsigned char* oro_runtime_init_get_user_config_bytes () { return reinterpret_cast<const unsigned char*>(""); }
  unsigned int oro_runtime_init_get_user_config_bytes_size () { return 0; }
  bool oro_runtime_init_is_debug_enabled () { return false; }
  const char* oro_runtime_init_get_dev_host () { return "localhost"; }
  int oro_runtime_init_get_dev_port () { return 0; }
  int oro_runtime_init_get_user_config_format () { return 0; }
}

using namespace oro::runtime;
using namespace oro::runtime::native;
struct PublicKeys : RecordKeyStore {
  std::map<std::string, RecordKey> entries;
  std::optional<RecordKey> get (const std::string& scope, char slot) override {
    const auto found = entries.find(scope + slot);
    return found == entries.end() ? std::optional<RecordKey>() : found->second;
  }
  void put (const std::string& scope, char slot, const RecordKey& key) override { entries[scope + slot] = key; }
  void remove (const std::string& scope, char slot) override { entries.erase(scope + slot); }
};
template <class F> void until (F ready) {
  const auto expires = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!ready()) { assert(std::chrono::steady_clock::now() < expires); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
}
template <class F> void fails (const std::string& code, F call) {
  try { call(); assert(false); } catch (const StateError& error) { assert(error.code == code); }
}
std::string phase (DeferredController& jobs, const std::string& id) {
  return jobs.request("status", JSON::Object::Entries {{"id", id}}).as<JSON::Object>().get("phase").as<JSON::String>().value();
}
int main (int argc, char** argv) {
  assert(argc == 2);
  auto keys = std::make_shared<PublicKeys>();
  auto lease = std::make_shared<int>(1);
  RecordStore store(std::filesystem::path(argv[1]) / "deferred.db", keys, "deferred-test", {"authority", "account", "identity"});
  std::atomic<int64_t> time = 1000000;
  std::atomic<bool> network = true;
  std::atomic<int> calls = 0;
  DeferredExecutor complete;
  complete.authorize = [](const auto& task, const auto& signal) { return task.authorityRef == "authority" && !signal.cancelled(); };
  complete.execute = [&](const auto&, const auto&) { calls++; return DeferredOutcome::Completed; };
  JSON::Object options(JSON::Object::Entries {{"id", "read"}, {"kind", "public.read"}, {"authorityRef", "authority"},
    {"accountRef", "account"}, {"targetRef", "target"}, {"operationRef", "read"}, {"network", "any"}});
  {
    DeferredController jobs(store, lease, [&](const auto&, bool, bool) { return network.load(); }, [&] { return time.load(); });
    jobs.registerRead("public.read", complete);
    auto bad = options; bad["password"] = "public-test-input";
    fails("EINVAL", [&] { jobs.request("enqueue", bad); });
    auto first = jobs.request("enqueue", options).as<JSON::Object>();
    until([&] { return phase(jobs, "read") == "completed"; });
    jobs.request("enqueue", options);
    assert(calls == 1);
    auto conflicting = options; conflicting["accountRef"] = "different";
    fails("EEXIST", [&] { jobs.request("enqueue", conflicting); });
    jobs.request("release", JSON::Object::Entries {{"id", "read"}});
    DeferredExecutor retry = complete;
    retry.execute = [&](const auto&, const auto&) { calls++; return DeferredOutcome::Retry; };
    jobs.registerRead("public.retry", retry);
    auto retryOptions = options; retryOptions["kind"] = "public.retry"; retryOptions["id"] = "retry";
    jobs.request("enqueue", retryOptions);
    for (int attempt = 1; attempt <= 5; attempt++) {
      until([&] {
        const auto current = jobs.request("status", JSON::Object::Entries {{"id", "retry"}}).as<JSON::Object>();
        return current.get("phase").as<JSON::String>().value() == (attempt == 5 ? "failed" : "retry") &&
          current.get("attempts").as<JSON::Number>().value() == attempt;
      });
      const auto state = jobs.request("status", JSON::Object::Entries {{"id", "retry"}}).as<JSON::Object>();
      assert(state.get("attempts").as<JSON::Number>().value() == attempt);
      if (attempt < 5) { time += (30000LL << (attempt - 1)); jobs.wake(); }
    }
    assert(calls == 6);
    fails("EINVAL", [&] { jobs.request("resume", JSON::Object::Entries {{"id", "retry"}}); });
    jobs.request("release", JSON::Object::Entries {{"id", "retry"}});
    fails("EINVAL", [&] { jobs.registerMaintenance("public.cleanup", {"cleanup"}, false, complete); });
    jobs.registerMaintenance("public.cleanup", {"cleanup"}, true, complete);
    auto maintenance = options; maintenance["id"] = "cleanup"; maintenance["kind"] = "public.cleanup"; maintenance["operationRef"] = "revoke";
    fails("ENOSYS", [&] { jobs.request("enqueueMaintenance", maintenance); });
    maintenance["operationRef"] = "cleanup";
    jobs.request("enqueueMaintenance", maintenance);
    until([&] { return phase(jobs, "cleanup") == "completed"; });
    jobs.request("release", JSON::Object::Entries {{"id", "cleanup"}});
    auto periodic = options; periodic["id"] = "periodic"; periodic["intervalMs"] = 900000;
    jobs.request("enqueue", periodic);
    time += 600000; jobs.wake();
    until([&] { return jobs.request("status", JSON::Object::Entries {{"id", "periodic"}}).as<JSON::Object>().get("completedRuns").as<JSON::Number>().value() == 1; });
    assert(phase(jobs, "periodic") == "scheduled");
    jobs.request("cancel", JSON::Object::Entries {{"id", "periodic"}});
    const auto before = calls.load(); time += 900000; jobs.wake();
    assert(phase(jobs, "periodic") == "cancelled" && calls == before);
    jobs.request("release", JSON::Object::Entries {{"id", "periodic"}});
    network = false;
    auto interrupted = options; interrupted["id"] = "interrupted";
    jobs.request("enqueue", interrupted);
    assert(phase(jobs, "interrupted") == "scheduled");
    jobs.shutdown();
    assert(calls == before);
  }
  const auto before = calls.load();
  DeferredController restarted(store, lease, [&](const auto&, bool, bool) { return network.load(); }, [&] { return time.load(); });
  restarted.registerRead("public.read", complete);
  assert(phase(restarted, "interrupted") == "interrupted");
  network = true;
  restarted.wake();
  assert(calls == before);
  restarted.request("resume", JSON::Object::Entries {{"id", "interrupted"}});
  until([&] { return phase(restarted, "interrupted") == "completed"; });
  assert(calls == before + 1);
  std::cout << "native-deferred-constraints-budgets-maintenance-and-restart-passed\n";
}
