#include "src/runtime/native/desktop_push.hh"
#include <cassert>
#include <fstream>
#include <iostream>

using namespace oro::runtime::native;
struct Keys : RecordKeyStore {
  std::map<std::pair<std::string, char>, RecordKey> values;
  std::string hiddenScope;
  std::optional<RecordKey> get (const std::string& scope, char slot) override {
    if (scope == hiddenScope)
      return std::nullopt;
    const auto found = values.find ({scope, slot});
    return found == values.end () ? std::optional<RecordKey> () : found->second;
  }
  void put (const std::string& scope, char slot, const RecordKey& key) override {
    values[{scope, slot}] = key;
  }
  void remove (const std::string& scope, char slot) override {
    assert (!values.contains ({scope, slot}));
  }
};
RecordBytes bytes (const std::filesystem::path& path) {
  std::ifstream input (path, std::ios::binary);
  return {std::istreambuf_iterator<char> (input), std::istreambuf_iterator<char> ()};
}
int main (int argc, char** argv) {
  assert (argc == 2);
  const auto directory = std::filesystem::path (argv[1]);
  const auto keys = std::make_shared<Keys> ();
  const auto original = directory / "original.sqlite";
  RecordStore legacy (original, keys, "push-transport", {"public", "runtime", "installation"});
  legacy.apply (0, {{"distributor", RecordBytes{'{', '}'}}});
  const auto before = bytes (original);
  const auto originalKeys = keys->values;
  keys->hiddenScope = legacy.scope ();  // Simulate an unavailable original key without deleting/replacing it.
  auto ledger = std::make_shared<PushJournalLedger> (directory / "control" / "selector.sqlite", "public.application");
  std::optional<RecordStore> fresh;
  const auto factory = [&] (const std::string& epoch) {
    if (epoch.empty ())
      return legacy;
    fresh.emplace (directory / ("fresh-" + epoch + ".sqlite"), keys, "push-transport",
                   RecordIdentity{"public", "runtime", "installation"});
    return *fresh;
  };
  auto bus = std::make_shared<DesktopBus> ();
  const auto lease = std::make_shared<int> (1);
  DesktopPush push (
      bus, legacy, "org.oro.PublicJournal.NativePush", [] (auto) { return false; }, lease, {}, false, ledger, factory);
  const auto state = push.inspectJournalRecovery ();
  assert (state.recoverable && !state.readable && state.reason == "missing-key");
  const auto result = push.quarantineUnreadableJournal ({state.revision, "public-native-recovery", true, true});
  assert (result.changed && !result.epoch.empty ());
  assert (keys->values == originalKeys && bytes (original) == before);
  assert (push.selectedDistributor ().empty ());
  assert (push.inspectJournalRecovery ().readable);
  assert (fresh);
  fresh->apply (0, {{"public", RecordBytes{'o', 'k'}}});
  const auto retry = push.quarantineUnreadableJournal ({state.revision, "public-native-recovery", true, true});
  assert (!retry.changed && fresh->read ().records.at ("public") == RecordBytes ({'o', 'k'}));
  assert (bytes (original) == before && keys->values.size () == originalKeys.size () + 1);
  for (const auto& [identity, key] : originalKeys)
    assert (keys->values.at (identity) == key);
  keys->hiddenScope.clear ();
  assert (legacy.read ().records.at ("distributor") == RecordBytes ({'{', '}'}));
  std::cout << "linux-push-journal-adapter-preserves-original-ciphertext-keys-and-idempotent-fresh-state-passed\n";
}
