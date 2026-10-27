#include "src/runtime/native/records.hh"

#include <sodium.h>
extern "C" {
#if __has_include("sqlite/sqlite3.h")
#include "sqlite/sqlite3.h"
#else
#include <sqlite3.h>
#endif
}

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>

using namespace oro::runtime::native;
namespace fs = std::filesystem;

// Public fixture only. Production uses SecureStorage::nativeRecordKeys(), never key files.
class PublicKeys final : public RecordKeyStore {
  public:
    fs::path root;
    bool unavailable = false;
    bool crashOnPut = false;
    int created = 0;
    explicit PublicKeys (fs::path root) : root(root) { fs::create_directories(root); }
    std::optional<RecordKey> get (const std::string& scope, char slot) override {
      if (unavailable) throw StateError("EACCES");
      const auto path = root / (scope + slot);
      if (!fs::exists(path)) return {};
      RecordKey key;
      std::ifstream input(path, std::ios::binary);
      input.read(reinterpret_cast<char*>(key.data()), key.size());
      assert(input.gcount() == 32);
      return key;
    }
    void put (const std::string& scope, char slot, const RecordKey& key) override {
      if (unavailable) throw StateError("EACCES");
      std::ofstream output(root / (scope + slot), std::ios::binary);
      output.write(reinterpret_cast<const char*>(key.data()), key.size());
      output.close();
      created++;
      if (crashOnPut) std::_Exit(77);
    }
    void remove (const std::string& scope, char slot) override {
      if (unavailable) throw StateError("EACCES");
      // No deletion in the fixture: mark the alias absent without discarding test evidence.
      const auto path = root / (scope + slot);
      if (fs::exists(path)) fs::rename(path, root / (scope + slot + ".retired." + std::to_string(created)));
    }
};

RecordBytes bytes (const std::string& value) { return {value.begin(), value.end()}; }
template <typename F> void error (const std::string& code, F action) {
  try { action(); throw std::runtime_error("Expected " + code); } catch (const StateError& error) { assert(error.code == code); }
}
void sql (const fs::path& path, const std::string& command) {
  sqlite3* database = nullptr;
  assert(sqlite3_open(path.string().c_str(), &database) == SQLITE_OK);
  assert(sqlite3_exec(database, command.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
  assert(sqlite3_close(database) == SQLITE_OK);
}
int main (int argc, char** argv) {
  assert(argc == 3);
  const fs::path root = argv[1];
  fs::create_directories(root);
  auto keys = std::make_shared<PublicKeys>(root / "public-test-keys");
  const auto database = root / "native-records-v2.db";
  RecordStore store(database, keys, "public.records", {"authority", "account", "identity"});
  RecordCoordinator coordinator(store);
  const std::string mode = argv[2];
  if (mode == "seed") {
    auto prepared = coordinator.prepare("interrupted", "public-operation", store.read().revision);
    coordinator.begin(prepared.ticket);
    std::cout << "seeded-executing\n";
    return 0;
  }
  if (mode == "recover") {
    const auto old = coordinator.get("interrupted");
    assert(old && old->phase == "executing" && !old->ownerProcessMatches);
    const auto uncertain = coordinator.recover(old->ticket);
    const auto resolved = coordinator.resolve(uncertain.ticket, true, {{"confirmed", bytes("public-result")}}, bytes("public-receipt"));
    assert(resolved.phase == "committed" && resolved.receipt == bytes("public-receipt"));
    coordinator.release(resolved.ticket);
    assert(!coordinator.get("interrupted"));
    std::cout << "recovered-without-replay\n";
    return 0;
  }
  if (mode == "crash-key") {
    keys->crashOnPut = true;
    store.apply(0, {{"value", bytes("public-value")}}, "public-commit");
    return 2;
  }
  if (mode == "recover-key") {
    assert(store.read().revision == 0 && !store.status().keyPresent);
    const auto value = store.apply(0, {{"value", bytes("public-value")}}, "public-commit");
    assert(value.revision == 1 && keys->created == 0);
    std::cout << "recovered-reserved-key\n";
    return 0;
  }
  if (mode == "lost-reply") {
    store.apply(0, {{"value", bytes("public-value")}}, "public-commit");
    std::_Exit(78);
  }
  if (mode == "read-reply") {
    assert(store.read().revision == 1 && store.read().commitId == "public-commit");
    error("ESTALE", [&] { store.apply(0, {{"value", bytes("duplicate")}}); });
    std::cout << "lost-reply-reconciled\n";
    return 0;
  }
  assert(mode == "matrix");
  assert(store.read().revision == 0 && !store.status().keyPresent && keys->created == 0);
  const auto secret = bytes("public unique plaintext marker should be encrypted");
  const auto first = store.apply(0, {{"value", secret}}, "initial");
  assert(first.revision == 1 && first.records.at("value") == secret);
  std::ifstream raw(database, std::ios::binary);
  const std::string disk((std::istreambuf_iterator<char>(raw)), std::istreambuf_iterator<char>());
  assert(disk.find("public unique plaintext marker") == std::string::npos);
  error("ESTALE", [&] { store.apply(0, {}); });
  error("EINVAL", [&] { store.apply(1, {{"@private", secret}}); });
  error("ENOSPC", [&] { store.apply(1, {{"huge", RecordBytes(65537)}}); });
  assert(store.read().revision == 1);
  auto second = store.migrate("public-migration", std::string(64, 'a'), 1, {{"imported", secret}});
  const auto repeated = store.migrate("public-migration", std::string(64, 'a'), second.revision, {{"ignored", secret}});
  assert(repeated.revision == second.revision && !repeated.records.contains("ignored"));
  error("EEXIST", [&] { store.migrate("public-migration", std::string(64, 'b'), repeated.revision, {}); });
  auto prepared = coordinator.prepare("operation", "public-operation", repeated.revision);
  error("EBUSY", [&] { coordinator.release(prepared.ticket); });
  auto executing = coordinator.begin(prepared.ticket);
  error("ESTALE", [&] { coordinator.begin(prepared.ticket); });
  error("EINVAL", [&] { coordinator.abort(executing.ticket); });
  auto unknown = coordinator.unknown(executing.ticket);
  error("ESTALE", [&] { coordinator.commit(executing.ticket, {}); });
  error("EINVAL", [&] { coordinator.resolve(unknown.ticket, false, {{"bad", secret}}); });
  auto done = coordinator.resolve(unknown.ticket, true, {{"result", secret}}, bytes("receipt"));
  assert(store.read().records.at("result") == secret && coordinator.get("operation")->receipt == bytes("receipt"));
  const auto visible = store.read();
  assert(std::none_of(visible.records.begin(), visible.records.end(), [](const auto& entry) { return entry.first.starts_with('@'); }));
  coordinator.release(done.ticket);
  auto isolated = RecordStore(database, keys, "public.records", {"authority", "other", "identity"});
  assert(isolated.read().revision == 0);
  isolated.apply(0, {{"isolated", secret}});
  auto separate = RecordStore(root / "second.db", keys, "public.records", {"authority", "account", "identity"});
  assert(separate.scope() != store.scope());
  separate.apply(0, {{"separate", secret}});
  separate.reset(1);
  assert(store.read().records.at("value") == secret);
  const auto before = store.read();
  std::atomic<int> successes = 0;
  std::atomic<int> stale = 0;
  auto writer = [&] {
    try { store.apply(before.revision, {{"concurrent", secret}}); successes++; }
    catch (const StateError& failure) { assert(failure.code == "ESTALE"); stale++; }
  };
  std::thread a(writer), b(writer);
  a.join(); b.join();
  assert(successes == 1 && stale == 1);
  keys->unavailable = true;
  error("EACCES", [&] { store.read(); });
  error("EACCES", [&] { store.status(); });
  keys->unavailable = false;
  auto state = store.read();
  const auto scope = store.scope();
  keys->remove(scope, 'a');
  assert(store.status().requiresRecovery);
  error("EKEYREVOKED", [&] { store.read(); });
  error("EKEYREVOKED", [&] { store.apply(state.revision, {}); });
  auto reset = store.reset(state.revision);
  assert(reset.revision > state.revision && reset.records.empty());
  assert(isolated.read().records.at("isolated") == secret);
  store.collectRetiredKey();
  state = store.apply(reset.revision, {{"after-reset", secret}});
  sql(database, "CREATE TRIGGER public_failure BEFORE UPDATE ON native_scopes BEGIN SELECT RAISE(ABORT, 'public-fault'); END");
  error("EIO", [&] { store.apply(state.revision, {{"after-reset", bytes("bad")}}); });
  sql(database, "DROP TRIGGER public_failure");
  assert(store.read().revision == state.revision && store.read().records.at("after-reset") == secret);
  sql(database, "UPDATE native_scopes SET envelope = zeroblob(length(envelope)) WHERE scope='" + scope + "'");
  error("EBADMSG", [&] { store.read(); });
  assert(store.status().revision == state.revision);
  const auto recovered = store.reset(state.revision);
  assert(recovered.records.empty() && recovered.revision == state.revision + 1);
  sql(database, "UPDATE native_scopes SET envelope = X'00' WHERE scope='" + scope + "'");
  error("EBADMSG", [&] { store.read(); });
  assert(store.reset(recovered.revision).revision == recovered.revision + 1);
  auto interruptedStore = RecordStore(database, keys, "public.initial-failure", {"authority", "account", "identity"});
  const auto beforeCreation = keys->created;
  sql(database, "CREATE TRIGGER public_initial_failure BEFORE UPDATE ON native_scopes BEGIN SELECT RAISE(ABORT, 'public-fault'); END");
  error("EIO", [&] { interruptedStore.apply(0, {{"new", secret}}); });
  assert(interruptedStore.read().revision == 0 && keys->created == beforeCreation + 1);
  sql(database, "DROP TRIGGER public_initial_failure");
  interruptedStore.apply(0, {{"new", secret}});
  assert(keys->created == beforeCreation + 1);
  RecordValues tooMany;
  for (int i = 0; i < 129; ++i) tooMany["key-" + std::to_string(i)] = {};
  error("ENOSPC", [&] { interruptedStore.migrate("large", std::string(64, 'a'), 1, tooMany); });
  const auto quota = root / "quota.db";
  for (size_t i = 0; i < RecordStore::maximumScopes; ++i) {
    RecordStore entry(quota, keys, "public.quota", {"authority", "account", std::to_string(i)});
    entry.apply(0, {});
  }
  RecordStore overflow(quota, keys, "public.quota", {"authority", "account", "overflow"});
  error("ENOSPC", [&] { overflow.apply(0, {}); });
  assert(!overflow.status().keyPresent);
  std::cout << "native-records-matrix-passed\n";
}
