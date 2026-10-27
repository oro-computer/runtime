#include "src/runtime/runtime.hh"
#include "src/runtime/native/records.hh"
#include <cassert>
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
int main(int argc, char** argv) {
  assert(argc == 3);
  const std::string mode = argv[2];
  std::shared_ptr<native::RecordKeyStore> keys;
  std::string scope;
  {
    Runtime::Options options;
    options.autoStart = false;
    options.userConfig["meta_bundle_identifier"] = "com.oro.public.parity";
    Runtime runtime(options);
    assert(runtime.services.state.init());
    assert(runtime.services.secureStorage.start());
    keys = runtime.services.secureStorage.nativeRecordKeys();
    native::RecordStore store(std::filesystem::path(argv[1]) / "public.db", keys,
      "public-check", {"public-authority", "public-account", "public-identity"});
    if (mode == "unavailable") {
      try { store.read(); return 4; }
      catch (const native::StateError& e) { assert(e.code == "EACCES"); }
      std::cout << "linux-secret-service-unavailable-fails-closed\n";
      return 0;
    }
    if (mode == "reopen") {
      assert(store.read().revision == 2);
      assert(store.read().records.empty());
      store.apply(2, {{"public", native::RecordBytes{'o', 'k'}}}, "public-reopened");
      assert(store.read().revision == 3);
      std::cout << "linux-secret-service-process-reopen-passed\n";
      return 0;
    }
    assert(mode == "write");
    assert(store.read().revision == 0);
    const auto value = store.apply(0, {{"public", native::RecordBytes{'o', 'k'}}}, "public-commit");
    assert(value.revision == 1 && store.read().records.at("public").size() == 2);
    assert(store.status().keyPresent);
    scope = store.scope();
    assert(store.reset(1).records.empty());
    store.collectRetiredKey();
    assert(runtime.services.secureStorage.stop());
    try { keys->get(scope, 'a'); return 2; }
    catch(const native::StateError& e) { assert(e.code == "EACCES"); }
    assert(runtime.services.secureStorage.start());
    assert(store.read().revision == 2);
  }
  try { keys->get(scope, 'a'); return 3; }
  catch(const native::StateError& e) { assert(e.code == "EACCES"); }
  std::cout << "linux-secret-service-native-records-passed\n";
}
