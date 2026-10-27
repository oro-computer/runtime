#include "src/runtime/native/protected_keys.hh"
#include <sodium.h>
#include <dlfcn.h>
#include <cassert>
#include <iostream>
#include <thread>

using namespace oro::runtime;
using namespace oro::runtime::native;

// Public fixture keys in memory only; applications use OS Secret Service keys.
class PublicKeys : public RecordKeyStore {
public:
  std::map<std::string, RecordKey> keys;
  std::optional<RecordKey> get (const std::string& scope, char slot) override {
    const auto found = keys.find(scope + slot);
    return found == keys.end() ? std::nullopt : std::optional(found->second);
  }
  void put (const std::string& scope, char slot, const RecordKey& value) override {
    keys[scope + slot] = value;
  }
  void remove (const std::string& scope, char slot) override {
    keys.erase(scope + slot);
  }
};
template <typename F>
void fails (const std::string& expected, F action) {
  try {
    action();
    throw std::runtime_error("Expected " + expected);
  } catch (const StateError& error) {
    if (error.code != expected)
      throw std::runtime_error("Expected " + expected + ", got " + error.code);
  }
}
int main (int argc, char** argv) {
  assert(argc == 4);
  Pkcs11Options options;
  options.module = argv[2];
  if (std::string(argv[3]) == "software") {
    assert(discoverPkcs11(options).empty());
    assert(
      !ProtectedKeys::status(options, "biometric-or-device", true).get("canAuthenticate").as<JSON::Boolean>().value());
    std::cout << "linux-protected-keys-software-slot-rejected\n";
    return 0;
  }
  options.provider = argv[3];
  auto library = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
  assert(library);
  const auto mode = reinterpret_cast<void (*)(unsigned int)>(dlsym(library, "oro_public_pkcs11_mode"));
  const auto logins = reinterpret_cast<unsigned int (*)(bool)>(dlsym(library, "oro_public_pkcs11_logins"));
  assert(mode && logins);
  const auto keys = std::make_shared<PublicKeys>();
  RecordStore store(std::filesystem::path(argv[1]) / "public-keys.sqlite", keys, "public-keys",
                    {"public", "account", argv[3]});
  std::atomic<int> prompts = 0;
  std::atomic<bool> wrong = false;
  ProtectedKeys provider(store, options, "com.oro.public.keys." + std::filesystem::path(argv[1]).filename().string(),
                         std::make_shared<int>(1), [&] (const auto&, const auto& signal) {
                           assert(!signal->cancelled());
                           prompts++;
                           const std::string pin = wrong ? "public-wrong-pin" : "public-test-user-pin";
                           return RecordBytes(pin.begin(), pin.end());
                         });
  auto invoke = [&] (const std::string& action, const JSON::Object& input) {
    return provider.request(action, input, nullptr, std::make_shared<ExecutionCancellation>(60000));
  };
  auto input = [] (const std::string& id) {
    return JSON::Object(JSON::Object::Entries{{"id", id}, {"policy", "biometric-or-device"}});
  };
  auto sealedInput = input("secret");
  sealedInput["value"] = "cHVibGljIHRlc3Q=";
  assert(!ProtectedKeys::status(options, "biometric", true).get("supported").as<JSON::Boolean>().value());
  assert(keys->keys.empty());
  mode(32);
  assert(discoverPkcs11(options).empty());
  mode(0);
  const auto status = ProtectedKeys::status(options, "biometric-or-device", true);
  assert(status.get("canAuthenticate").as<JSON::Boolean>().value());
  const auto contextBefore = logins(true);
  auto sealed = invoke("seal", sealedInput).as<JSON::Object>();
  assert(logins(true) == contextBefore + 1);
  auto openInput = input("secret");
  openInput["sealed"] = sealed;
  assert(invoke("open", openInput).as<JSON::Object>().get("value").as<JSON::String>().value() == "cHVibGljIHRlc3Q=");
  auto modified = sealed;
  auto ciphertext = modified.get("ciphertext").as<JSON::String>().value();
  ciphertext[0] = ciphertext[0] == 'A' ? 'B' : 'A';
  modified["ciphertext"] = ciphertext;
  openInput["sealed"] = modified;
  fails("EBADMSG", [&] { invoke("open", openInput); });
  openInput["sealed"] = sealed;
  wrong = true;
  fails("EACCES", [&] { invoke("open", openInput); });
  wrong = false;
  mode(64);
  fails("ENODEV", [&] { invoke("open", openInput); });
  mode(1);
  fails("EKEYREVOKED", [&] { invoke("open", openInput); });
  mode(0);
  auto challenge = input("signing");
  challenge["authority"] = "https://127.0.0.1:9443";
  challenge["accountRef"] = "public-account";
  challenge["challenge"] = "MDEyMzQ1Njc4OWFiY2RlZg==";
  challenge["expiresAt"] = static_cast<int64_t>(g_get_real_time() / 1000 + 240000);
  const auto enrolled = invoke("createSigningKey", challenge).as<JSON::Object>();
  const auto signedAgain = invoke("signChallenge", challenge).as<JSON::Object>();
  assert(enrolled.get("publicKey").str() == signedAgain.get("publicKey").str());
  {
    RecordStore otherStore(std::filesystem::path(argv[1]) / "other-app.sqlite", keys, "public-keys",
      {"public", "account", "other-bundle"});
    ProtectedKeys other(otherStore, options,
      "com.oro.other.keys." + std::filesystem::path(argv[1]).filename().string(), std::make_shared<int>(1),
      [](const auto&, const auto&) {
        const std::string pin = "public-test-user-pin";
        return RecordBytes(pin.begin(), pin.end());
      });
    const auto otherSignature = other.request("createSigningKey", challenge, nullptr,
      std::make_shared<ExecutionCancellation>(60000)).as<JSON::Object>();
    assert(enrolled.get("publicKey").str() != otherSignature.get("publicKey").str());
    auto retire = input("signing");
    retire["kind"] = "signing";
    other.request("removeKey", retire, nullptr, std::make_shared<ExecutionCancellation>(60000));
    other.shutdown();
    other.drain();
    assert(otherStore.read().records.empty());
  }
  mode(2);
  fails("EBADMSG", [&] { invoke("signChallenge", challenge); });
  mode(8);
  auto signal = std::make_shared<ExecutionCancellation>(60000);
  std::string result;
  const auto before = logins(true);
  std::thread interrupted([&] {
    try {
      provider.request("signChallenge", challenge, nullptr, signal);
    } catch (const StateError& error) {
      result = error.code;
    }
  });
  for (int i = 0; i < 500 && logins(true) == before; i++)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  assert(logins(true) == before + 1);
  signal->cancel();
  interrupted.join();
  assert(result == "ABORT_ERR" && provider.busy());
  fails("EBUSY", [&] { invoke("signChallenge", challenge); });
  provider.drain();
  mode(4);
  auto failedCreate = challenge;
  failedCreate["id"] = "failed-create";
  fails("EIO", [&] { invoke("createSigningKey", failedCreate); });
  mode(0);
  fails("EUNKNOWN", [&] { invoke("createSigningKey", failedCreate); });
  auto removeFailed = input("failed-create");
  removeFailed["kind"] = "signing";
  invoke("removeKey", removeFailed);
  auto removal = input("secret");
  removal["kind"] = "secret";
  mode(16);
  fails("EUNKNOWN", [&] { invoke("removeKey", removal); });
  mode(0);
  fails("EUNKNOWN", [&] { invoke("open", openInput); });
  invoke("removeKey", removal);
  fails("ENOENT", [&] { invoke("open", openInput); });
  removal = input("signing");
  removal["kind"] = "signing";
  invoke("removeKey", removal);
  fails("ENOENT", [&] { invoke("signChallenge", challenge); });
  assert(store.read().records.empty());
  assert(prompts >= 10 && logins(false) > logins(true));
  provider.shutdown();
  provider.drain();
  std::cout << "linux-protected-keys-provider-and-failure-checks-passed:" << argv[3] << '\n';
}
